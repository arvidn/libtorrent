/*

Copyright (c) 2014-2015, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include <boost/bind.hpp>

#include "setup_swarm.hpp"

namespace lt = libtorrent;
using namespace sim;

namespace {

struct swarm
{
	swarm(int num_nodes, swarm_setup_provider& config)
		: m_config(config)
		, m_ios(m_sim, asio::ip::address_v4::from_string("0.0.0.0"))
		, m_start_time(lt::clock_type::now())
		, m_timer(m_ios)
		, m_shutting_down(false)
		, m_tick(0)
	{

		for (int i = 0; i < num_nodes; ++i)
		{
			// create a new io_service
			char ep[30];
			snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
			m_io_service.push_back(boost::make_shared<sim::asio::io_service>(
				boost::ref(m_sim), asio::ip::address_v4::from_string(ep)));

			lt::settings_pack pack = m_config.add_session(i);

			boost::shared_ptr<lt::session> ses =
				boost::make_shared<lt::session>(pack
					, boost::ref(*m_io_service.back()));
			m_nodes.push_back(ses);
			m_config.on_session_added(i, *ses);

			// reserve a slot in here for when the torrent gets added (notified by
			// an alert)
			m_torrents.push_back(lt::torrent_handle());

			lt::add_torrent_params params = m_config.add_torrent(i);
			ses->async_add_torrent(params);

			ses->set_alert_notify(boost::bind(&swarm::on_alert_notify, this, i));
		}

		m_timer.expires_from_now(lt::seconds(1));
		m_timer.async_wait(boost::bind(&swarm::on_tick, this, _1));
	}

	void on_tick(lt::error_code const& ec)
	{
		if (ec || m_shutting_down) return;

		lt::time_duration d = lt::clock_type::now() - m_start_time;
		boost::uint32_t millis = lt::duration_cast<lt::milliseconds>(d).count();
		printf("%4d.%03d: TICK %d\n", millis / 1000, millis % 1000, m_tick);

		++m_tick;

		if (m_tick > 120)
		{
			terminate();
			return;
		}

		m_timer.expires_from_now(lt::seconds(1));
		m_timer.async_wait(boost::bind(&swarm::on_tick, this, _1));
	}

	void on_alert_notify(int session_index)
	{
		// this function is called inside libtorrent and we cannot perform work
		// immediately in it. We have to notify the outside to pull all the alerts
		m_io_service[session_index]->post(boost::bind(&swarm::on_alerts, this, session_index));
	}

	void on_alerts(int session_index)
	{
		std::vector<lt::alert*> alerts;

		lt::session* ses = m_nodes[session_index].get();

		// when shutting down, we may have destructed the session
		if (ses == NULL) return;

		bool term = false;
		ses->pop_alerts(&alerts);

		for (std::vector<lt::alert*>::iterator i = alerts.begin();
			i != alerts.end(); ++i)
		{
			lt::alert* a = *i;

			lt::time_duration d = a->timestamp() - m_start_time;
			boost::uint32_t millis = lt::duration_cast<lt::milliseconds>(d).count();
			printf("%4d.%03d: [%02d] %s\n", millis / 1000, millis % 1000,
				session_index, a->message().c_str());

			// if a torrent was added save the torrent handle
			if (lt::add_torrent_alert* at = lt::alert_cast<lt::add_torrent_alert>(a))
			{
				lt::torrent_handle h = at->handle;
				m_torrents[session_index] = h;

				// let the config object have a chance to set up the torrent
				m_config.on_torrent_added(session_index, h);

				// now, connect this torrent to all the others in the swarm
				for (int k = 0; k < session_index; ++k)
				{
					char ep[30];
					snprintf(ep, sizeof(ep), "50.0.%d.%d", (k + 1) >> 8, (k + 1) & 0xff);
					h.connect_peer(lt::tcp::endpoint(
						lt::address_v4::from_string(ep), 6881));
				}
			}

			if (m_config.on_alert(a, session_index, m_torrents
					, *m_nodes[session_index]))
				term = true;
		}

		if (term) terminate();
	}

	void run()
	{
		m_sim.run();
		printf("simulation::run() returned\n");
	}

	void terminate()
	{
		printf("TERMINATING\n");

		m_config.on_exit(m_torrents);

		// terminate simulation
		for (int i = 0; i < int(m_nodes.size()); ++i)
		{
			m_zombies.push_back(m_nodes[i]->abort());
			m_nodes[i].reset();
		}

		m_shutting_down = true;
	}

private:

	swarm_setup_provider& m_config;

	sim::default_config cfg;
	sim::simulation m_sim{cfg};
	asio::io_service m_ios;
	lt::time_point m_start_time;

	std::vector<boost::shared_ptr<lt::session> > m_nodes;
	std::vector<boost::shared_ptr<sim::asio::io_service> > m_io_service;
	std::vector<lt::torrent_handle> m_torrents;
	std::vector<lt::session_proxy> m_zombies;
	lt::deadline_timer m_timer;
	bool m_shutting_down;
	int m_tick;
};

} // anonymous namespace

void setup_swarm(int num_nodes, swarm_setup_provider& cfg)
{
	swarm s(num_nodes, cfg);
	s.run();
}

