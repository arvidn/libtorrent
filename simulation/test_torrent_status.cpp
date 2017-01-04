/*

Copyright (c) 2015, Arvid Norberg
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

#include "test.hpp"
#include "setup_swarm.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/settings_pack.hpp"

using namespace libtorrent;
using namespace sim;
namespace lt = libtorrent;

// this is a test for torrent_status time counters are correct
// it is a long running test, we did already try to improve it:
// - less slow peers vs many fast peers
// - slow peers with smal torrent vs fast peers with large torrent
// - less peers with large torrents vs more peers with smal torrent
// - connection limit vs parallel requests
// as conlusion the following combination take the least amount of
// execution time per tick:
// - many slow peers with small torrent and connection limit
TORRENT_TEST(status_timers_time_shift_with_paused_torrent)
{
	lt::time_point startseed_time;
	lt::time_point last_active_time;
	lt::torrent_handle handle;
	bool ran_to_completion = false;
	seconds expected_active_duration = seconds(0);
	bool tick_is_in_active_range = false;

	// TODO: this setup takes 1 minute execution time and could be improved by
	// simulate more time per tick, currently we execute 71000 ticks
	setup_swarm(10, swarm_test::upload_slow
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(lt::settings_pack::alert_mask, alert::status_notification
				| alert::torrent_log_notification);
			// slow as possible to do less compute per tick, but not too slow
			// because simulator want to get done in 88 ticks per node
			// 2KiB/s is very close to limit and could fail if the seed code get
			// less efficient or if the simulator use more data by default
			pack.set_int(settings_pack::upload_rate_limit, 2*1024);
			pack.set_int(settings_pack::connections_limit, 2);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<torrent_added_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				lt::time_point start_time = lt::clock_type::now();
				handle = ta->handle;
				torrent_status st = handle.status();
				// test last upload and download state before wo go throgh
				// torrent states
				TEST_CHECK(st.last_download == start_time);
				TEST_CHECK(st.last_upload == start_time);
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{
			if(tick_is_in_active_range){
				// 1 second per tick
				expected_active_duration++;
			}

			// resume on a tick which is far away from next tick check intervall
			// because with very low upload limit it takes some time to unchock
			switch(ticks)
			{
				case 0:
					// torrent get ready for seeding on first tick, means time +1s
					startseed_time = lt::clock_type::now();
					tick_is_in_active_range = true;
					break;
				case 21:
					// pause on tick 21 after we did have the first upload tick
					// this takes some ticks because of the speed limit
					handle.pause();
					tick_is_in_active_range = false;
					last_active_time = lt::clock_type::now();
					break;
				case 64001:
					// resum just before we hit the time shift handling
					handle.resume();
					tick_is_in_active_range = true;
					break;
				case 64061:
					// pause after some some upload ticks
					// not every tick is an upload because of the speed limit
					handle.pause();
					tick_is_in_active_range = false;
					last_active_time = lt::clock_type::now();
					break;
				case 70001:
					// resum after the time shift to figure out if an upload time
					// does still work
					handle.resume();
					tick_is_in_active_range = true;
					break;
				case 70061:
					// simulate at least 70061 seconds because peer timestamps are
					// 16 bits counting seconds, so they can only represent about
					// 18 hours
					ran_to_completion = true;
					return true;
			}
			// verify that the timers seem correct
			// at least 11s intervall because after resume it takes 11s untill we unchuck
			// new pieces at this slow upload rate
			if (tick_is_in_active_range && (ticks % 20) == 0)
			{
				lt::time_point now = lt::clock_type::now();
				torrent_status st = handle.status();
				TEST_CHECK(st.active_duration == expected_active_duration);
				TEST_CHECK(st.seeding_duration == expected_active_duration);
				TEST_CHECK(st.finished_duration == expected_active_duration);
				// does not download in seeding mode
				TEST_CHECK(st.last_download == startseed_time);
				// TODO: write a converter for TEST_EQUAL from duration to rep
				// convert like this: return duration_cast<seconds>(arg1).count();
				// test.hpp: error: no match for ‘operator<<’ operand types are
				// std::basic_ostream<char> and std::chrono::duration<long int>
				// then we can remove the printf statements
				printf("expected_active_duration: %" PRId64 " \n",
					expected_active_duration.count());
				printf("active_duration: %" PRId64 " \n",
					st.active_duration.count());
				printf("seeding_duration: %" PRId64 " \n",
					st.seeding_duration.count());
				printf("finished_duration: %" PRId64 " \n",
					st.finished_duration.count());

				// upload is running, last should be now
				// we don't upload every tick because we have an upload limit
				// at 2KiB/s limit there should be at least an update every 20 ticks
				printf("seconds since last upload: %" PRId64 " \n",
					total_seconds(now - st.last_upload));
				TEST_CHECK(total_seconds(now - st.last_upload) <= 20);
			}

			// check if upload time stay on last date with paused torrent
			// check before and after 16bit overflow
			if(ticks == 64200 || ticks == 67000 )
			{
				TEST_EQUAL(tick_is_in_active_range, false);
				torrent_status st = handle.status();
				// we don't upload every tick because we have an upload limit
				// at this limit there should be at least an update every 20 ticks
				TEST_CHECK(total_seconds(last_active_time - st.last_upload) <= 20);
			}

			return false;
		});
	TEST_CHECK(ran_to_completion);
}


//
TORRENT_TEST(status_timers_time_shift_with_active_torrent)
{
	bool ran_to_completion = false;

	lt::torrent_handle handle;
	seconds expected_active_duration = seconds(0);
	bool tick_is_in_active_range = false;
	int tick_check_intervall = 1;
	lt::time_point startseed_time;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<torrent_added_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				lt::time_point start_time = lt::clock_type::now();
				handle = ta->handle;
				torrent_status st = handle.status();
				// test last upload and download state before wo go throgh
				// torrent states
				TEST_CHECK(st.last_download == start_time);
				TEST_CHECK(st.last_upload == start_time);
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{
			if(tick_is_in_active_range){
				// 1 second per tick
				expected_active_duration++;
				printf("tick %d\n", ticks);
			}

			switch(ticks)
			{
				case 0:
					// torrent get ready for seeding on first tick, means time +1s
					startseed_time = lt::clock_type::now();
					tick_is_in_active_range = true;
					break;
				case 1:
					// pause after we did have the first upload tick
					handle.pause();
					tick_is_in_active_range = false;
					break;
				case 64000:
					// resum just before we hit the time shift handling
					// this is needed to test what happend if we want to
					// shift more time then we have active time because
					// we shift 4 hours and have less then 1 hours active time
					handle.resume();
					tick_is_in_active_range = true;
					// don't check every tick
					tick_check_intervall = 600;
					break;
				case 68000:
					// simulate at least 68000 seconds because timestamps are
					// 16 bits counting seconds
					ran_to_completion = true;
					return true;
			}

			// verify that the timers seem correct
			if (tick_is_in_active_range && (ticks % tick_check_intervall) == 0)
			{
				torrent_status st = handle.status();
				TEST_CHECK(st.active_duration == expected_active_duration);
				TEST_CHECK(st.seeding_duration == expected_active_duration);
				TEST_CHECK(st.finished_duration == expected_active_duration);
				// does not download in seeding mode
				TEST_CHECK(st.last_download == startseed_time);
				// does not upload without peers
				TEST_CHECK(st.last_download == startseed_time);

				// TODO: write a converter for TEST_EQUAL from duration to rep
				printf("expected_active_duration: %" PRId64 " \n",
					expected_active_duration.count());
				printf("active_duration: %" PRId64 " \n",
					st.active_duration.count());
				printf("seeding_duration: %" PRId64 " \n",
					st.seeding_duration.count());
				printf("finished_duration: %" PRId64 " \n",
					st.finished_duration.count());
			}
			return false;
		});
	TEST_CHECK(!ran_to_completion);
}

// This test makes sure that adding a torrent causes no torrent related alert to
// be posted _before_ the add_torrent_alert, which is expected to always be the
// first
TORRENT_TEST(alert_order)
{
	bool received_add_torrent_alert = false;
	int num_torrent_alerts = 0;

	lt::torrent_handle handle;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack& sett) {
			sett.set_int(settings_pack::alert_mask, alert::all_categories);
		}
		// add torrent
		, [](lt::add_torrent_params ) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<add_torrent_alert>(a))
			{
				TEST_EQUAL(received_add_torrent_alert, false);
				received_add_torrent_alert = true;
				handle = ta->handle;
			}

			if (auto ta = dynamic_cast<torrent_alert const*>(a))
			{
				TEST_EQUAL(received_add_torrent_alert, true);
				TEST_CHECK(handle == ta->handle);
				++num_torrent_alerts;
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{ return ticks > 10; }
	);

	TEST_EQUAL(received_add_torrent_alert, true);
	TEST_CHECK(num_torrent_alerts > 1);
}

