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

using namespace lt;
using namespace sim;

time_point32 time_now()
{
	return lt::time_point_cast<seconds32>(lt::clock_type::now());
}

template <typename Tp1, typename Tp2>
bool eq(Tp1 const lhs, Tp2 const rhs)
{
	return std::abs(lt::duration_cast<seconds>(lhs - rhs).count()) <= 2;
}

// this is a test for torrent_status time counters are correct
TORRENT_TEST(status_timers)
{
	lt::time_point32 start_time;
	lt::torrent_handle handle;
	bool ran_to_completion = false;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<add_torrent_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				start_time = time_now();
				handle = ta->handle;
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{

			// simulate 20 hours of uptime. Currently, the session_time and internal
			// peer timestamps are 16 bits counting seconds, so they can only
			// represent about 18 hours. The clock steps forward in 4 hour increments
			// to stay within that range
			if (ticks > 20 * 60 * 60)
			{
				ran_to_completion = true;
				return true;
			}

			// once an hour, verify that the timers seem correct
			if ((ticks % 3600) == 0)
			{
				lt::time_point32 const now = time_now();
				// finish is 1 tick after start
				auto const since_finish = duration_cast<seconds>(now - start_time);
				torrent_status st = handle.status();
				TEST_EQUAL(st.active_duration.count(), since_finish.count());
				TEST_EQUAL(st.seeding_duration.count(), since_finish.count());
				TEST_EQUAL(st.finished_duration.count(), since_finish.count());

				// does not upload without peers
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
				// does not download in seeding mode
				TEST_CHECK(st.last_download == time_point(seconds(0)));
			}
			return false;
		});
	TEST_CHECK(ran_to_completion);
}

TORRENT_TEST(status_timers_last_upload)
{
	bool ran_to_completion = false;

	lt::torrent_handle handle;

	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<add_torrent_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				handle = ta->handle;
				torrent_status st = handle.status();
				// test last upload and download state before wo go throgh
				// torrent states
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
				TEST_CHECK(st.last_download == time_point(seconds(0)));
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{
			if (ticks > 10)
			{
				ran_to_completion = true;
				return true;
			}

			torrent_status st = handle.status();
			// upload time is 0 seconds behind now
			TEST_CHECK(eq(st.last_upload, time_now()));
			// does not download in seeding mode
			TEST_CHECK(st.last_download == time_point(seconds(0)));
			return false;
		});
	TEST_CHECK(ran_to_completion);
}

TORRENT_TEST(status_timers_time_shift_with_active_torrent)
{
	bool ran_to_completion = false;

	lt::torrent_handle handle;
	seconds expected_active_duration = seconds(1);
	bool tick_is_in_active_range = false;
	int tick_check_intervall = 1;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<add_torrent_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				handle = ta->handle;
				torrent_status st = handle.status();
				// test last upload and download state before wo go throgh
				// torrent states
				TEST_CHECK(st.last_download == time_point(seconds(0)));
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{
			if(tick_is_in_active_range){
				// 1 second per tick
				expected_active_duration++;
			}

			switch(ticks)
			{
				case 0:
					// torrent get ready for seeding on first tick, means time +1s
					tick_is_in_active_range = true;
					break;
				case 1:
					// pause after we did have the first upload tick
					handle.pause();
					tick_is_in_active_range = false;
					break;
				case 64000:
					// resume just before we hit the time shift handling
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
				TEST_EQUAL(st.active_duration.count(), expected_active_duration.count());
				TEST_EQUAL(st.seeding_duration.count(), expected_active_duration.count());
				TEST_EQUAL(st.finished_duration.count(), expected_active_duration.count());
				// does not upload without peers
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
				// does not download in seeding mode
				TEST_CHECK(st.last_download == time_point(seconds(0)));
			}
			return false;
		});
	TEST_CHECK(ran_to_completion);
}

TORRENT_TEST(finish_time_shift_active)
{
	bool ran_to_completion = false;

	lt::torrent_handle handle;
	seconds expected_active_duration = seconds(1);
	bool tick_is_in_active_range = false;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<add_torrent_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				handle = ta->handle;
				torrent_status st = handle.status();
				// test last upload and download state before wo go throgh
				// torrent states
				TEST_CHECK(st.last_download == time_point(seconds(0)));
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{
			if(tick_is_in_active_range){
				// 1 second per tick
				expected_active_duration++;
			}

			switch(ticks)
			{
				case 0:
					// torrent get ready for seeding on first tick, means time +1s
					tick_is_in_active_range = true;
					break;
				case 7000:
					// pause before 4 hours to get a become finish timestamp which
					// will be clamped
					handle.pause();
					// resume to get an become finish update
					handle.resume();
					tick_is_in_active_range = true;
					break;
				case 70000:
					// simulate at least 70000 seconds because timestamps are
					// 16 bits counting seconds
					ran_to_completion = true;
					return true;
			}

			// verify that the timers seem correct
			if ((ticks % 3600) == 0)
			{
				torrent_status st = handle.status();
				TEST_EQUAL(st.active_duration.count(), expected_active_duration.count());
				TEST_EQUAL(st.seeding_duration.count(), expected_active_duration.count());
				TEST_EQUAL(st.finished_duration.count(), expected_active_duration.count());
				// does not upload without peers
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
				// does not download in seeding mode
				TEST_CHECK(st.last_download == time_point(seconds(0)));
			}
			return false;
		});
	TEST_CHECK(ran_to_completion);
}

TORRENT_TEST(finish_time_shift_paused)
{
	bool ran_to_completion = false;

	lt::torrent_handle handle;
	seconds expected_active_duration = seconds(1);
	bool tick_is_in_active_range = false;

	setup_swarm(1, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto ta = alert_cast<add_torrent_alert>(a))
			{
				TEST_CHECK(!handle.is_valid());
				handle = ta->handle;
				torrent_status st = handle.status();
				// test last upload and download state before wo go throgh
				// torrent states
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
				TEST_CHECK(st.last_download == time_point(seconds(0)));
			}
		}
		// terminate
		, [&](int ticks, lt::session&) -> bool
		{
			if(tick_is_in_active_range){
				// 1 second per tick
				expected_active_duration++;
			}

			switch(ticks)
			{
				case 0:
					// torrent get ready for seeding on first tick, means time +1s
					tick_is_in_active_range = true;
					break;
				case 7000:
					// pause before 4 hours to get a become finish timestamp which
					// will be clamped
					handle.pause();
					// resume to get an become finish update
					handle.resume();
					// pause to test timeshift in paused state
					handle.pause();
					tick_is_in_active_range = false;
					break;
				case 70000:
					// simulate at least 70000 seconds because timestamps are
					// 16 bits counting seconds
					ran_to_completion = true;
					return true;
			}

			// verify that the timers seem correct
			if (tick_is_in_active_range && (ticks % 3600) == 0)
			{
				torrent_status st = handle.status();
				TEST_EQUAL(st.active_duration.count(), expected_active_duration.count());
				TEST_EQUAL(st.seeding_duration.count(), expected_active_duration.count());
				TEST_EQUAL(st.finished_duration.count(), expected_active_duration.count());
				// does not upload without peers
				TEST_CHECK(st.last_upload == time_point(seconds(0)));
				// does not download in seeding mode
				TEST_CHECK(st.last_download == time_point(seconds(0)));
			}
			return false;
		});
	TEST_CHECK(ran_to_completion);
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
			sett.set_int(settings_pack::alert_mask, alert_category::all);
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

