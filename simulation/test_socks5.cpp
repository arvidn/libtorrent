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
#include "setup_transfer.hpp" // for addr()
#include "simulator/simulator.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "fake_peer.hpp"
#include <iostream>

using namespace sim;
using namespace lt;


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

	lt::add_torrent_params params = ::create_torrent(1);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.save_path = save_path(0);
	ses->async_add_torrent(params);

	// set up a timer to fire later, to verify everything we expected to happen
	// happened
	sim::timer t(sim, lt::seconds(100), [&](boost::system::error_code const&)
	{
		std::printf("shutting down\n");
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	test(sim, *ses, params.ti);
}

TORRENT_TEST(socks5_tcp_announce)
{
	using namespace lt;
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
		[&alert_port](lt::session&, lt::alert const* alert) {
			if (auto* a = lt::alert_cast<lt::listen_succeeded_alert>(alert))
			{
				if (a->socket_type == socket_type_t::udp)
				{
					alert_port = a->port;
				}
			}
		},
		[&tracker_port](sim::simulation& sim, lt::session&
			, std::shared_ptr<lt::torrent_info> ti)
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
					std::string::size_type port_pos = req.find("&port=");
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

	TEST_EQUAL(tracker_port, 6881);
	TEST_CHECK(alert_port != -1);
}

TORRENT_TEST(udp_tracker)
{
	using namespace lt;
	bool tracker_alert = false;
	bool connected = false;
	bool announced = false;
	run_test(
		[](lt::session& ses)
		{
			set_proxy(ses, settings_pack::socks5);

			// The socks server in libsimulator does not support forwarding UDP
			// packets to hostnames (just IPv4 destinations)
			settings_pack p;
			p.set_bool(settings_pack::proxy_hostnames, false);
			ses.apply_settings(p);

			lt::add_torrent_params params;
			params.info_hash = sha1_hash("abababababababababab");
			params.trackers.push_back("udp://2.2.2.2:8080/announce");
			params.save_path = ".";
			ses.async_add_torrent(params);
		},
		[&tracker_alert](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::tracker_announce_alert>(alert))
				tracker_alert = true;
		},
		[&](sim::simulation& sim, lt::session&
			, std::shared_ptr<lt::torrent_info> ti)
		{
			// listen on port 8080
			udp_server tracker(sim, "2.2.2.2", 8080,
			[&](char const* msg, int size)
			{
				using namespace lt::detail;
				std::vector<char> ret;
				TEST_CHECK(size >= 16);

				if (size < 16) return ret;

				std::uint64_t connection_id = read_uint64(msg);
				std::uint32_t action = read_uint32(msg);
				std::uint32_t transaction_id = read_uint32(msg);

				std::uint64_t const conn_id = 0xfeedface1337ull;

				if (action == 0)
				{
					std::printf("udp connect\n");
					// udp tracker connect
					TEST_CHECK(connection_id == 0x41727101980ull);
					auto inserter = std::back_inserter(ret);
					write_uint32(0, inserter); // connect
					write_uint32(transaction_id, inserter);
					write_uint64(conn_id, inserter);
					connected = true;
				}
				else if (action == 1)
				{
					std::printf("udp announce\n");
					// udp tracker announce
					TEST_EQUAL(connection_id, conn_id);

					auto inserter = std::back_inserter(ret);
					write_uint32(1, inserter); // announce
					write_uint32(transaction_id, inserter);
					write_uint32(1800, inserter);
					write_uint32(0, inserter); // leechers
					write_uint32(0, inserter); // seeders
					announced = true;
				}
				else
				{
					std::printf("unsupported udp tracker action: %d\n", action);
				}
				return ret;
			});

			sim.run();
		}
	);

	TEST_CHECK(tracker_alert);
	TEST_CHECK(connected);
	TEST_CHECK(announced);
}

TORRENT_TEST(socks5_udp_retry)
{
	// this test is asserting that when a UDP associate command fails, we have a
	// 5 second delay before we try again. There is no need to actually add a
	// torrent for this test, just to open the udp socket with a socks5 proxy
	using namespace libtorrent;

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_service> ios = make_io_service(sim, 0);
	lt::session_proxy zombie;

	sim::asio::io_service proxy_ios{sim, addr("50.50.50.50") };
	// close UDP associate connectons prematurely
	sim::socks_server socks5(proxy_ios, 5555, 5, socks_flag::disconnect_udp_associate);

	lt::settings_pack pack = settings();
	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);
	print_alerts(*ses);
	set_proxy(*ses, settings_pack::socks5);

	sim::timer t(sim, lt::seconds(60), [&](boost::system::error_code const&)
	{
		fprintf(stderr, "shutting down\n");
		// shut down
		zombie = ses->abort();
		ses.reset();
	});
	sim.run();

	// number of UDP ASSOCIATE commands invoked on the socks proxy
	// We run for 60 seconds. The sokcks5 retry interval is expected to be 5
	// seconds, meaning there should have been 12 connection attempts
	TEST_EQUAL(socks5.cmd_counts()[2], 12);
}
