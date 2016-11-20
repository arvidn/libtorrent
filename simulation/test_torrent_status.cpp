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
TORRENT_TEST(status_timers)
{
	lt::time_point start_time;
	lt::torrent_handle handle;

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
				start_time = lt::clock_type::now();
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
				return true;

			// once an hour, verify that the timers seem correct
			if ((ticks % 3600) == 0)
			{
				lt::time_point now = lt::clock_type::now();
				auto const since_start = duration_cast<seconds>(now - start_time) - lt::seconds(1);
				torrent_status st = handle.status();
				TEST_CHECK(st.active_duration == since_start);
				TEST_CHECK(st.seeding_duration == since_start);
				TEST_CHECK(st.finished_duration == since_start);
				TEST_CHECK(st.last_upload < start_time);

				// checking the torrent counts as downloading
				// eventually though, we've forgotten about it and go back to -1
				if (since_start > lt::seconds(65000))
				{
					TEST_CHECK(st.last_download < start_time);
				}
				else
				{
					// TODO: this should really be a proximity-check
					TEST_CHECK(st.last_download == start_time + lt::seconds(1));
				}
			}

			return false;
		});
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

