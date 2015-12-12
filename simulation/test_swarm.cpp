/*

Copyright (c) 2008, Arvid Norberg
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
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"

using namespace libtorrent;

TORRENT_TEST(seed_mode)
{
	// with seed mode
	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= add_torrent_params::flag_seed_mode;
		}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{ return true; });
}

TORRENT_TEST(plain)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 75)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			printf("completed in %d ticks\n", ticks);
			return true;
		});
}

TORRENT_TEST(suggest)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 75)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			printf("completed in %d ticks\n", ticks);
			return true;
		});
}

TORRENT_TEST(utp_only)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_bool(settings_pack::enable_incoming_utp, true);
			pack.set_bool(settings_pack::enable_outgoing_utp, true);
			pack.set_bool(settings_pack::enable_incoming_tcp, false);
			pack.set_bool(settings_pack::enable_outgoing_tcp, false);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 75)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			return true;
		});
}

void test_stop_start_download(swarm_test type, bool graceful)
{
	bool paused_once = false;
	bool resumed = false;

	setup_swarm(3, type
		// add session
		, [](lt::settings_pack& pack) {
			// this test will pause and resume the torrent immediately, we expect
			// to reconnect immediately too, so disable the min reconnect time
			// limit.
			pack.set_int(settings_pack::min_reconnect_time, 0);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {

		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {

			if (lt::alert_cast<lt::torrent_added_alert>(a))
				add_extra_peers(ses);

			if (auto tp = lt::alert_cast<lt::torrent_paused_alert>(a))
			{
				TEST_EQUAL(resumed, false);
				printf("\nSTART\n\n");
				tp->handle.resume();
				resumed = true;
			}
		}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (paused_once == false)
			{
				auto st = get_status(ses);
				const bool limit_reached = (type == swarm_test::download)
					? st.total_wanted_done > st.total_wanted / 2
					: st.total_payload_upload >= 3 * 16 * 1024;

				if (limit_reached)
				{
					printf("\nSTOP\n\n");
					auto h = ses.get_torrents()[0];
					h.pause(graceful ? torrent_handle::graceful_pause : 0);
					paused_once = true;
				}
			}

			printf("tick: %d\n", ticks);

			const int timeout = type == swarm_test::download ? 20 : 91;
			if (ticks > timeout)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (type == swarm_test::upload) return false;
			if (!is_seed(ses)) return false;
			printf("completed in %d ticks\n", ticks);
			return true;
		});

	TEST_EQUAL(paused_once, true);
	TEST_EQUAL(resumed, true);
}

TORRENT_TEST(stop_start_download)
{
	test_stop_start_download(swarm_test::download, false);
}

TORRENT_TEST(stop_start_download_graceful)
{
	test_stop_start_download(swarm_test::download, true);
}

TORRENT_TEST(stop_start_download_graceful_no_peers)
{
	bool paused_once = false;
	bool resumed = false;

	setup_swarm(1, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {
			if (auto tp = lt::alert_cast<lt::torrent_paused_alert>(a))
			{
				TEST_EQUAL(resumed, false);
				printf("\nSTART\n\n");
				tp->handle.resume();
				resumed = true;
			}
		}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (paused_once == false
				&& ticks == 6)
			{
				printf("\nSTOP\n\n");
				auto h = ses.get_torrents()[0];
				h.pause(torrent_handle::graceful_pause);
				paused_once = true;
			}

			printf("tick: %d\n", ticks);

			// when there's only one node (i.e. no peers) we won't ever download
			// the torrent. It's just a test to make sure we still get the
			// torrent_paused_alert
			return ticks > 60;
		});

	TEST_EQUAL(paused_once, true);
	TEST_EQUAL(resumed, true);
}


TORRENT_TEST(stop_start_seed)
{
	test_stop_start_download(swarm_test::upload, false);
}

TORRENT_TEST(stop_start_seed_graceful)
{
	test_stop_start_download(swarm_test::upload, true);
}

TORRENT_TEST(explicit_cache)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
			pack.set_bool(settings_pack::explicit_read_cache, true);
			pack.set_int(settings_pack::explicit_cache_interval, 5);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 75)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			return true;
		});
}

TORRENT_TEST(shutdown)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (completed_pieces(ses) == 0) return false;
			TEST_EQUAL(is_seed(ses), false);
			return true;
		});
}

// TODO: add test that makes sure a torrent in graceful pause mode won't make
// outgoing connections
// TODO: add test that makes sure a torrent in graceful pause mode won't accept
// incoming connections
// TODO: test allow-fast
// TODO: test the different storage allocation modes
// TODO: test contiguous buffers


