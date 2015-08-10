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
#include "libtorrent/time.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include <boost/bind.hpp>

#include "setup_dht.hpp"

namespace lt = libtorrent;
using namespace sim;

namespace {

struct network
{
	network(int num_nodes, network_setup_provider& config)
		: m_config(config)
		, m_ios(m_sim, asio::ip::address_v4::from_string("10.0.0.1"))
		, m_start_time(lt::clock_type::now())
		, m_timer(m_ios)
		, m_shutting_down(false)
	{

		for (int i = 0; i < num_nodes; ++i)
		{
			// create a new io_service
			char ep[30];
			snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
			m_io_service.push_back(boost::make_shared<sim::asio::io_service>(
				boost::ref(m_sim), asio::ip::address_v4::from_string(ep)));

			lt::settings_pack pack = m_config.add_session(i);

			boost::shared_ptr<lt::session> ses = boost::make_shared<lt::session>(pack
				, boost::ref(*m_io_service.back()));
			m_nodes.push_back(ses);

			m_config.setup_session(*ses, i);

			ses->set_alert_notify(boost::bind(&network::on_alert_notify, this, i));
		}

		m_timer.expires_from_now(lt::seconds(1));
		m_timer.async_wait(boost::bind(&network::on_tick, this, _1));

		sim::dump_network_graph(m_sim, "../dht-sim.dot");
	}

	void on_tick(lt::error_code const& ec)
	{
		if (ec || m_shutting_down) return;

		if (m_config.on_tick())
		{
			terminate();
			return;
		}

		m_timer.expires_from_now(lt::seconds(1));
		m_timer.async_wait(boost::bind(&network::on_tick, this, _1));
	}

	void on_alert_notify(int session_index)
	{
		// this function is called inside libtorrent and we cannot perform work
		// immediately in it. We have to notify the outside to pull all the alerts
		m_io_service[session_index]->post(boost::bind(&network::on_alerts, this, session_index));
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

			if (session_index == 0)
			{
				// only log the experience of node 0
				lt::time_duration d = a->timestamp() - m_start_time;
				boost::uint32_t millis = lt::duration_cast<lt::milliseconds>(d).count();
				printf("%4d.%03d: [%02d] %s\n", millis / 1000, millis % 1000,
					session_index, a->message().c_str());
			}

			if (m_config.on_alert(a, session_index))
				term = true;

			if (lt::alert_cast<lt::listen_succeeded_alert>(a))
			{
				// add a single DHT node to bootstrap from. Make everyone bootstrap
				// from the node added 3 steps earlier (this makes the distribution a
				// bit unrealisticly uniform).
				int dht_bootstrap = (std::max)(0, session_index - 3);

				char ep[50];
				snprintf(ep, sizeof(ep), "50.0.%d.%d", (dht_bootstrap + 1) >> 8, (dht_bootstrap + 1) & 0xff);
				ses->add_dht_node(std::pair<std::string, int>(
						ep, m_nodes[dht_bootstrap]->listen_port()));
			}
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

		m_config.on_exit();

		// terminate simulation
		for (int i = 0; i < int(m_nodes.size()); ++i)
		{
			m_zombies.push_back(m_nodes[i]->abort());
			m_nodes[i].reset();
		}

		m_shutting_down = true;
	}

private:

	network_setup_provider& m_config;

	sim::default_config cfg;
	sim::simulation m_sim{cfg};
	asio::io_service m_ios;
	lt::time_point m_start_time;

	std::vector<boost::shared_ptr<lt::session> > m_nodes;
	std::vector<boost::shared_ptr<sim::asio::io_service> > m_io_service;
	std::vector<lt::session_proxy> m_zombies;
	lt::deadline_timer m_timer;
	bool m_shutting_down;
};

} // anonymous namespace

void setup_dht(int num_nodes, network_setup_provider& cfg)
{
	network s(num_nodes, cfg);
	s.run();
}


