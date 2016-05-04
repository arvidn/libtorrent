/*

Copyright (c) 2016, Arvid Norberg
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
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "simulator/http_server.hpp"
#include "settings.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "fake_peer.hpp"
#include <iostream>

using namespace sim;
using namespace libtorrent;

namespace lt = libtorrent;

std::unique_ptr<sim::asio::io_service> make_io_service(sim::simulation& sim, int i)
{
	char ep[30];
	snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
	return std::unique_ptr<sim::asio::io_service>(new sim::asio::io_service(
		sim, address_v4::from_string(ep)));
}

// this is the general template for these tests. create the session with custom
// settings (Settings), set up the test, by adding torrents with certain
// arguments (Setup), run the test and verify the end state (Test)
template <typename Setup, typename HandleAlerts, typename Test>
void run_test(Setup const& setup
	, HandleAlerts const& on_alert
	, Test const& test)
{
	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_service> ios = make_io_service(sim, 0);
	lt::session_proxy zombie;

	sim::asio::io_service proxy_ios{sim, addr("50.50.50.50") };
	sim::socks_server socks4(proxy_ios, 4444, 4);
	sim::socks_server socks5(proxy_ios, 5555, 5);

	lt::settings_pack pack = settings();
	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);

	// set up test, like adding torrents (customization point)
	setup(*ses);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses, [=](lt::session& ses, lt::alert const* a) {
		on_alert(ses, a);
	});

	lt::add_torrent_params params = create_torrent(1);
	params.flags &= ~lt::add_torrent_params::flag_auto_managed;
	params.flags &= ~lt::add_torrent_params::flag_paused;
	params.save_path = save_path(0);
	ses->async_add_torrent(params);

	// set up a timer to fire later, to verify everything we expected to happen
	// happened
	sim::timer t(sim, lt::seconds(100), [&](boost::system::error_code const& ec)
	{
		fprintf(stderr, "shutting down\n");
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	test(sim, *ses, params.ti);
}

TORRENT_TEST(socks5_tcp_accept)
{
	using namespace libtorrent;
	bool incoming_connection = false;
	run_test(
		[](lt::session& ses)
		{
			set_proxy(ses, settings_pack::socks5);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (auto* a = lt::alert_cast<lt::incoming_connection_alert>(alert))
			{
				TEST_EQUAL(a->socket_type, 2);
				incoming_connection = true;
			}
		},
		[](sim::simulation& sim, lt::session& ses
			, boost::shared_ptr<lt::torrent_info> ti)
		{
			// test connecting to the client via its socks5 listen port
		// TODO: maybe we could use peer_conn here instead
			fake_peer peer1(sim, "60.0.0.0");
			fake_peer peer2(sim, "60.0.0.1");

			sim::timer t1(sim, lt::seconds(2), [&](boost::system::error_code const& ec)
			{
				peer1.connect_to(tcp::endpoint(addr("50.50.50.50"), 6881), ti->info_hash());
			});

			sim::timer t2(sim, lt::seconds(3), [&](boost::system::error_code const& ec)
			{
				peer2.connect_to(tcp::endpoint(addr("50.50.50.50"), 6881), ti->info_hash());
			});

			sim.run();
		}
	);

	TEST_EQUAL(incoming_connection, true);
}

TORRENT_TEST(socks4_tcp_accept)
{
	using namespace libtorrent;
	bool incoming_connection = false;
	run_test(
		[](lt::session& ses)
		{
			set_proxy(ses, settings_pack::socks4);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (auto* a = lt::alert_cast<lt::incoming_connection_alert>(alert))
			{
				TEST_EQUAL(a->socket_type, 2);
				TEST_EQUAL(a->ip.address(), addr("60.0.0.0"))
				incoming_connection = true;
			}
		},
		[](sim::simulation& sim, lt::session& ses
			, boost::shared_ptr<lt::torrent_info> ti)
		{
			fake_peer peer1(sim, "60.0.0.0");

			sim::timer t1(sim, lt::seconds(2), [&](boost::system::error_code const& ec)
			{
				peer1.connect_to(tcp::endpoint(addr("50.50.50.50"), 6881), ti->info_hash());
			});

			sim.run();
		}
	);

	TEST_EQUAL(incoming_connection, true);
}

// make sure a listen_succeeded_alert is issued when successfully listening on
// incoming connections via a socks5 proxy
TORRENT_TEST(socks4_tcp_listen_alert)
{
	using namespace libtorrent;
	bool listen_alert = false;
	run_test(
		[](lt::session& ses)
		{
			set_proxy(ses, settings_pack::socks4);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (auto* a = lt::alert_cast<lt::listen_succeeded_alert>(alert))
			{
				if (a->sock_type == listen_succeeded_alert::socks5)
				{
					TEST_EQUAL(a->endpoint.address(), addr("50.50.50.50"));
					TEST_EQUAL(a->endpoint.port(), 6881);
					listen_alert = true;
				}
			}
		},
		[](sim::simulation& sim, lt::session& ses
			, boost::shared_ptr<lt::torrent_info> ti)
		{
			sim.run();
		}
	);

	TEST_EQUAL(listen_alert, true);
}

TORRENT_TEST(socks5_tcp_listen_alert)
{
	using namespace libtorrent;
	bool listen_alert = false;
	run_test(
		[](lt::session& ses)
		{
			set_proxy(ses, settings_pack::socks5);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (auto* a = lt::alert_cast<lt::listen_succeeded_alert>(alert))
			{
				if (a->sock_type == listen_succeeded_alert::socks5)
				{
					TEST_EQUAL(a->endpoint.address(), addr("50.50.50.50"));
					TEST_EQUAL(a->endpoint.port(), 6881);
					listen_alert = true;
				}
			}
		},
		[](sim::simulation& sim, lt::session& ses
			, boost::shared_ptr<lt::torrent_info> ti)
		{
			sim.run();
		}
	);

	TEST_EQUAL(listen_alert, true);
}

TORRENT_TEST(socks5_tcp_announce)
{
	using namespace libtorrent;
	int tracker_port = -1;
	int alert_port = -1;
	run_test(
		[](lt::session& ses)
		{
			set_proxy(ses, settings_pack::socks5);

			lt::add_torrent_params params;
			params.info_hash = sha1_hash("abababababababababab");
			params.trackers.push_back("http://2.2.2.2:8080/announce");
			params.save_path = ".";
			ses.async_add_torrent(params);
		},
		[&alert_port](lt::session& ses, lt::alert const* alert) {
			if (auto* a = lt::alert_cast<lt::listen_succeeded_alert>(alert))
			{
				if (a->sock_type == listen_succeeded_alert::socks5)
				{
					alert_port = a->endpoint.port();
				}
			}
		},
		[&tracker_port](sim::simulation& sim, lt::session& ses
			, boost::shared_ptr<lt::torrent_info> ti)
		{
			sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
			// listen on port 8080
			sim::http_server http(web_server, 8080);

			http.register_handler("/announce"
				, [&tracker_port](std::string method, std::string req
				, std::map<std::string, std::string>&)
			{
				if (req.find("&event=started") != std::string::npos)
				{
					int port_pos = req.find("&port=");
					TEST_CHECK(port_pos != std::string::npos);
					if (port_pos != std::string::npos)
					{
						tracker_port = atoi(req.c_str() + port_pos + 6);
					}
				}

				return sim::send_response(200, "OK", 27) + "d8:intervali1800e5:peers0:e";
			});

			sim.run();
		}
	);

	TEST_EQUAL(alert_port, tracker_port);
	TEST_CHECK(alert_port != -1);
	TEST_CHECK(tracker_port != -1);
}

