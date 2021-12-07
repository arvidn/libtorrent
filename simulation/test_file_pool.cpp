/*

Copyright (c) 2016-2018, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "setup_swarm.hpp"
#include "test.hpp"
#include "utils.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/disk_interface.hpp"

using namespace lt;

// the disk I/O thread is not simulated with high enough fidelity for this to
// work
TORRENT_TEST(close_file_interval)
{
	bool ran_to_completion = false;

	// with seed mode
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::close_file_interval, 20);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			// terminate after 40 seconds
			if (ticks > 24)
			{
				ran_to_completion = true;
				return true;
			}

			torrent_handle h = ses.get_torrents().front();
			std::vector<open_file_state> const file_status = h.file_status();
			printf("%d: %d files\n", ticks, int(file_status.size()));
			if (ticks > 0 && ticks < 19)
			{
				TEST_EQUAL(file_status.size(), 1);
			}
			else if (ticks > 21)
			{
				// the close file timer shuold have kicked in at 20 seconds
				// and closed the file
				TEST_EQUAL(file_status.size(), 0);
			}
			return false;
		});
	TEST_CHECK(ran_to_completion);
}

TORRENT_TEST(file_pool_size)
{
	bool ran_to_completion = false;
	int max_files = 0;

	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack)
		{
			pack.set_int(lt::settings_pack::file_pool_size, 5);
		}
		// add torrent
		, [](lt::add_torrent_params& atp) {
			// we need a torrent with lots of files in it, to hit the
			// file_size_limit we set.
			file_storage fs;
			for (int i = 0; i < 0x10 * 9; ++i)
			{
				char filename[50];
				snprintf(filename, sizeof(filename), "root/file-%d", i);
				fs.add_file(filename, 0x400);
			}
			atp.ti = std::make_shared<torrent_info>(*atp.ti);
			atp.ti->remap_files(fs);
		}
		// on alert
		, [&](lt::alert const*, lt::session&) {}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 80)
			{
				TEST_ERROR("timeout");
				return true;
			}

			std::vector<open_file_state> const status = ses.get_torrents().at(0).file_status();
			printf("open files: %d\n", int(status.size()));
			max_files = std::max(max_files, int(status.size()));
			if (!is_seed(ses)) return false;
			printf("completed in %d ticks\n", ticks);
			ran_to_completion = true;
			return true;
		});

	TEST_CHECK(max_files <= 5);
	TEST_CHECK(max_files >= 4);
	TEST_CHECK(ran_to_completion);
}

