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

#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "settings.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp" // for timer
#include <iostream>

using namespace sim;
using namespace libtorrent;

const int num_torrents = 10;
namespace lt = libtorrent;

using sim::asio::ip::address_v4;

std::unique_ptr<sim::asio::io_service> make_io_service(sim::simulation& sim, int i)
{
	char ep[30];
	snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
	return std::unique_ptr<sim::asio::io_service>(new sim::asio::io_service(
		sim, asio::ip::address_v4::from_string(ep)));
}

// this is the general template for these tests. create the session with custom
// settings (Settings), set up the test, by adding torrents with certain
// arguments (Setup), run the test and verify the end state (Test)
template <typename Settings, typename Setup, typename Test>
void run_test(Settings const& sett, Setup const& setup, Test const& test)
{
	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_service> ios0 = make_io_service(sim, 0);
	std::unique_ptr<sim::asio::io_service> ios1 = make_io_service(sim, 1);
	lt::session_proxy zombie0;
	lt::session_proxy zombie1;

	// setup settings packs to use (customization point)
	lt::settings_pack pack0 = settings();
	lt::settings_pack pack1 = settings();
	sett(pack0, pack1);

	// create session
	std::shared_ptr<lt::session> ses0 = std::make_shared<lt::session>(pack0, *ios0);
	std::shared_ptr<lt::session> ses1 = std::make_shared<lt::session>(pack1, *ios1);

	// set up test, like adding torrents (customization point)
	setup(*ses0, *ses1);

	// set up a timer to fire later, to verify everything we expected to happen
	// happened
	sim::timer t(sim, lt::seconds((num_torrents + 1) * 60)
		, [&](boost::system::error_code const& ec)
	{
		test(*ses0, *ses1);

		// shut down
		zombie0 = ses0->abort();
		zombie1 = ses1->abort();
		ses0.reset();
		ses1.reset();
	});

	sim.run();
}

TORRENT_TEST(keep_redundant_connections)
{
	run_test(
		[](lt::settings_pack& sett0, lt::settings_pack& sett1) {
			// session 0
			sett0.set_int(lt::settings_pack::active_downloads, 1);
			sett0.set_int(lt::settings_pack::active_seeds, 1);
			// session 1
			sett1.set_int(lt::settings_pack::active_seeds, 1);
		},

		[](lt::session& ses0, lt::session& ses1) {
			// session 0: the downloader
			lt::add_torrent_params params0 = create_torrent(0, false);
			params0.flags |= lt::add_torrent_params::flag_keep_redundant_connections;
			ses0.async_add_torrent(params0);
			// session 1: the seed
			lt::add_torrent_params params1 = create_torrent(0, true);
			params1.flags |= lt::add_torrent_params::flag_keep_redundant_connections;
			ses1.async_add_torrent(params1);
		},

		[](lt::session& ses0, lt::session& ses1) {
			std::vector<lt::alert*> alerts;

			ses0.pop_alerts(&alerts);
			lt::time_point start_time = alerts[0]->timestamp();
			for (alert* a : alerts)
			{
				fprintf(stderr, "0:%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
			}

			ses1.pop_alerts(&alerts);
			for (alert* a : alerts)
			{
				fprintf(stderr, "1:%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
			}

			// session 0: the downloader
			for (torrent_handle const& h : ses0.get_torrents())
			{
				TEST_CHECK(h.keep_redundant_connections());
				TEST_CHECK(!h.status().paused);
				TEST_EQUAL(h.status().state, torrent_status::seeding);
				std::vector<lt::peer_info> peers;
				h.get_peer_info(peers);
				TEST_EQUAL(peers.size(), 1);
			}
			// session 1: the seed
			for (torrent_handle const& h : ses1.get_torrents())
			{
				TEST_CHECK(h.keep_redundant_connections());
				TEST_CHECK(!h.status().paused);
				std::vector<lt::peer_info> peers;
				h.get_peer_info(peers);
				TEST_EQUAL(peers.size(), 1);
			}
		});
}

