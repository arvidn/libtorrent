/*

Copyright (c) 2017, Arvid Norberg
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

#include "setup_swarm.hpp"
#include "test.hpp"
#include "utils.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp" // for timer
#include "settings.hpp"
#include "create_torrent.hpp"
#include "setup_transfer.hpp" // for addr()

using namespace lt;

TORRENT_TEST(seed_mode)
{
	// with seed mode
	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {
			// make sure the session works with a tick interval of 5 seconds
			pack.set_int(settings_pack::tick_interval, 5000);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
		}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int ticks, lt::session&) -> bool {
			// we don't need to finish seeding, exit after 20 seconds
			return ticks > 20;
		});
}

#ifndef TORRENT_DISABLE_LOGGING
TORRENT_TEST(ip_notifier_setting)
{
	int s_tick = 0;
	int working_count = 0;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack)
		{
			pack.set_int(settings_pack::tick_interval, 1000);
			pack.set_int(settings_pack::alert_mask, alert_category::all);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&s_tick, &working_count](lt::alert const* a, lt::session&)
		{
			std::string const msg = a->message();
			if (msg.find("received error on_ip_change:") != std::string::npos)
			{
				TEST_CHECK(s_tick == 0 || s_tick == 2);
				working_count++;
			}
		}
		// terminate
		, [&s_tick](int ticks, lt::session& ses) -> bool {

			if (ticks == 1)
			{
				settings_pack sp;
				sp.set_bool(settings_pack::enable_ip_notifier, false);
				ses.apply_settings(sp);
			}
			else if (ticks == 2)
			{
				settings_pack sp;
				sp.set_bool(settings_pack::enable_ip_notifier, true);
				ses.apply_settings(sp);
			}

			s_tick = ticks;

			// exit after 3 seconds
			return ticks > 3;
		});

	TEST_EQUAL(working_count, 2);
}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
struct test_plugin : lt::torrent_plugin
{
	bool m_new_connection = false;
	bool m_files_checked = false;

	std::shared_ptr<peer_plugin> new_connection(peer_connection_handle const&) override
	{
		m_new_connection = true;
		return std::shared_ptr<peer_plugin>();
	}

	void on_files_checked() override
	{
		m_files_checked = true;
	}
};

TORRENT_TEST(add_extension_while_transfer)
{
	bool done = false;
	auto p = std::make_shared<test_plugin>();

	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack)
		{
			pack.set_int(settings_pack::tick_interval, 1000);
			pack.set_int(settings_pack::alert_mask, alert_category::all);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&done, p](lt::alert const* a, lt::session&)
		{
			if (a->type() == peer_connect_alert::alert_type)
			{
				auto create_test_plugin = [p](torrent_handle const&, void*)
				{ return p; };

				lt::torrent_handle th = alert_cast<peer_connect_alert>(a)->handle;
				th.add_extension(create_test_plugin);

				done = true;
			}
		}
		// terminate
		, [&done](int ticks, lt::session&) -> bool
		{
			// exit after 10 seconds
			return ticks > 10 || done;
		});

	TEST_CHECK(done);
	TEST_CHECK(p->m_new_connection);
	TEST_CHECK(p->m_files_checked);
}
#endif // TORRENT_DISABLE_EXTENSIONS

// make sure TCP and UDP listen sockets use the same port
TORRENT_TEST(tie_listen_ports)
{
	using namespace libtorrent;

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_service ios { sim, addr("50.0.0.1")};

	lt::session_proxy zombie;

	// create session
	auto pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:0");
	pack.set_int(settings_pack::alert_mask, alert_category::error
		| alert_category::status
		| alert_category::torrent_log);

	auto ses = std::make_shared<lt::session>(pack, ios);

	std::vector<int> listen_ports;

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses, [&](lt::session&, lt::alert const* a){
		if (auto const* la = alert_cast<listen_succeeded_alert>(a))
		{
			listen_ports.push_back(la->port);
		}
	});

	sim::timer t(sim, lt::seconds(30), [&](boost::system::error_code const&)
	{
	// TEST
		zombie = ses->abort();
		ses.reset();

		TEST_CHECK(listen_ports.size() > 0);
		int const port = listen_ports.front();
		for (int const p : listen_ports)
			TEST_EQUAL(p, port);
	});

	sim.run();
}
