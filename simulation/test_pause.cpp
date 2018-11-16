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
#include "settings.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"
#include <iostream>

using namespace sim;
using namespace lt;

using sim::asio::ip::address_v4;

// this is the general template for these tests. create the session with custom
// settings (Settings), set up the test, by adding torrents with certain
// arguments (Setup), run the test and verify the end state (Test)
template <typename Setup, typename Torrent, typename Test, typename Check>
void run_test(Setup const& setup, Torrent const& torrent
	, Test const& test, Check const& check)
{
	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_service> ios = make_io_service(sim, 0);
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);

	setup(*ses);

	fake_peer p1(sim, "60.0.0.0");
	fake_peer p2(sim, "60.0.0.1");
	fake_peer p3(sim, "60.0.0.2");
	std::array<fake_peer*, 3> test_peers = {{ &p1, &p2, &p3 }};

	// add torrent
	lt::add_torrent_params params = ::create_torrent(0, false);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	ses->async_add_torrent(std::move(params));

	lt::torrent_handle h;
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		auto at = lt::alert_cast<add_torrent_alert>(a);
		if (at == nullptr) return;
		h = at->handle;

		// disable the print_alert object from polling any more alerts
		ses.set_alert_notify([]{});

		torrent(ses, h, test_peers);
	});

	sim::timer t1(sim, lt::seconds(5)
		, [&](boost::system::error_code const&)
	{
		test(*ses, h, test_peers);
	});

	// set up a timer to fire later, to verify everything we expected to happen
	// happened
	sim::timer t2(sim, lt::seconds(10)
		, [&](boost::system::error_code const&)
	{
		check(*ses, h, test_peers);

		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

// make sure the torrent disconnects all its peers when it's paused
TORRENT_TEST(torrent_paused_disconnect)
{
	run_test(
		[](lt::session&) {},
		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			add_fake_peers(h, 3);
		},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>& test_peers) {
			check_accepted(test_peers, {{true, true, true}});
			check_connected(test_peers, {{true, true, true}});
			check_disconnected(test_peers, {{false, false, false}});
			h.pause();
		},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>& test_peers) {
			check_disconnected(test_peers, {{true, true, true}});
			TEST_CHECK(h.status().flags & torrent_flags::paused);
		});
}

// make sure the torrent disconnects all its peers when the session is paused
TORRENT_TEST(session_paused_disconnect)
{
	run_test(
		[](lt::session&) {},
		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			add_fake_peers(h, 3);
		},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>& test_peers) {
			check_accepted(test_peers, {{true, true, true}});
			check_connected(test_peers, {{true, true, true}});
			check_disconnected(test_peers, {{false, false, false}});
			ses.pause();
		},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>& test_peers) {
			check_disconnected(test_peers, {{true, true, true}});

			// the torrent isn't paused, the session is
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
		});
}

// make sure a torrent is not connecting to any peers when added to a paused
// session
TORRENT_TEST(paused_session_add_torrent)
{
	run_test(
		[](lt::session& ses) { ses.pause(); },
		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			add_fake_peers(h, 3);
		},

		[](lt::session&, lt::torrent_handle, std::array<fake_peer*, 3>& test_peers) {
			check_accepted(test_peers, {{false, false, false}});
		},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			// the torrent isn't paused, the session is
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
		});
}

// make sure the torrent isn't connecting to peers when it's paused
TORRENT_TEST(paused_torrent_add_peers)
{
	run_test(
		[](lt::session&) {},
		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			h.pause();

			add_fake_peers(h, 3);
		},

		[](lt::session&, lt::torrent_handle, std::array<fake_peer*, 3>& test_peers) {
			check_accepted(test_peers, {{false, false, false}});
		},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(h.status().flags & torrent_flags::paused);
		});
}

// make sure we post the torrent_paused alert when pausing a torrent
TORRENT_TEST(torrent_paused_alert)
{
	run_test(
		[](lt::session&) {},
		[](lt::session&, lt::torrent_handle, std::array<fake_peer*, 3>&) {},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			h.pause();
		},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(h.status().flags & torrent_flags::paused);

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_resume = 0;
			int num_paused = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (lt::alert_cast<torrent_resumed_alert>(a)) ++num_resume;
				if (lt::alert_cast<torrent_paused_alert>(a)) ++num_paused;
			}

			TEST_EQUAL(num_resume, 0);
			TEST_EQUAL(num_paused, 1);
		});
}

// make sure we post the torrent_paused alert when pausing the session
TORRENT_TEST(session_paused_alert)
{
	run_test(
		[](lt::session&) {},
		[](lt::session&, lt::torrent_handle, std::array<fake_peer*, 3>&) {},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			ses.pause();
		},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_resume = 0;
			int num_paused = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (lt::alert_cast<torrent_resumed_alert>(a)) ++num_resume;
				if (lt::alert_cast<torrent_paused_alert>(a)) ++num_paused;
			}

			TEST_EQUAL(num_resume, 0);
			TEST_EQUAL(num_paused, 1);
		});
}

// make sure we post both the paused and resumed alert when pausing and resuming
// the session.
TORRENT_TEST(session_pause_resume)
{
	run_test(
		[](lt::session&) {},
		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			ses.pause();
		},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			ses.resume();
		},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_resume = 0;
			int num_paused = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (lt::alert_cast<torrent_resumed_alert>(a)) ++num_resume;
				if (lt::alert_cast<torrent_paused_alert>(a)) ++num_paused;
			}

			TEST_EQUAL(num_resume, 1);
			TEST_EQUAL(num_paused, 1);
		});
}

// make sure peers added to a (non-paused) torrent in a paused session are
// connected once the session is resumed
TORRENT_TEST(session_pause_resume_connect)
{
	run_test(
		[](lt::session&) {},
		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>&) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			ses.pause();
			add_fake_peers(h, 3);
		},

		[](lt::session& ses, lt::torrent_handle h, std::array<fake_peer*, 3>& test_peers) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			check_accepted(test_peers, {{false, false, false}});
			ses.resume();
		},

		[](lt::session&, lt::torrent_handle h, std::array<fake_peer*, 3>& test_peers) {
			TEST_CHECK(!(h.status().flags & torrent_flags::paused));

			check_accepted(test_peers, {{true, true, true}});
		});
}

