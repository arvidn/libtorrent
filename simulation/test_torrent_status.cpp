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
#include "swarm_config.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/alert_types.hpp"

using namespace libtorrent;
using namespace sim;
namespace lt = libtorrent;

// this is a test for torrent_status time counters are correct
struct test_swarm_config : swarm_config
{
	test_swarm_config()
		: swarm_config()
	{}

	bool on_alert(libtorrent::alert const* alert
		, int session_idx
		, std::vector<libtorrent::torrent_handle> const& handles
		, libtorrent::session& ses) override
	{
		if (torrent_added_alert const* ta = alert_cast<torrent_added_alert>(alert))
		{
			TEST_CHECK(!m_handle.is_valid());
			m_start_time = lt::clock_type::now();
			m_handle = ta->handle;
		}
		return false;
	}

	virtual void on_exit(std::vector<torrent_handle> const& torrents) override {}

	virtual bool tick(int t) override
	{
		// once an hour, verify that the timers seem correct
		if ((t % 3600) == 0)
		{
			lt::time_point now = lt::clock_type::now();
			int since_start = total_seconds(now - m_start_time) - 1;
			torrent_status st = m_handle.status();
			TEST_EQUAL(st.active_time, since_start);
			TEST_EQUAL(st.seeding_time, since_start);
			TEST_EQUAL(st.finished_time, since_start);

			TEST_EQUAL(st.last_scrape, -1);
			TEST_EQUAL(st.time_since_upload, -1);

			// checking the torrent counts as downloading
			// eventually though, we've forgotten about it and go back to -1
			if (since_start > 65000)
			{
				TEST_EQUAL(st.time_since_download, -1);
			}
			else
			{
				TEST_EQUAL(st.time_since_download, since_start);
			}
		}

		// simulate 20 hours of uptime. Currently, the session_time and internal
		// peer timestamps are 16 bits counting seconds, so they can only
		// represent about 18 hours. The clock steps forward in 4 hour increments
		// to stay within that range
		return t > 20 * 60 * 60;
	}

private:
	lt::time_point m_start_time;
	lt::torrent_handle m_handle;
};

TORRENT_TEST(status_timers)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	test_swarm_config cfg;
	setup_swarm(1, sim, cfg);
}

