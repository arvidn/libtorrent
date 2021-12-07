/*

Copyright (c) 2016-2017, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "setup_swarm.hpp"
#include "test.hpp"
#include "utils.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "settings.hpp"

using namespace lt;

TORRENT_TEST(seed_and_suggest_mode)
{
	add_torrent_params resume_data;

	// with seed mode
	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
		}
		// on alert
		, [&](lt::alert const* a, lt::session&)
		{
			auto* sr = alert_cast<save_resume_data_alert>(a);
			if (sr == nullptr) return;

			resume_data = sr->params;
		}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks < 5) return false;
			if (ticks == 5)
			{
				ses.get_torrents()[0].save_resume_data();
				return false;
			}
			return true;
		});

	printf("save-resume: %s\n", write_resume_data(resume_data).to_string().c_str());
	TEST_CHECK(resume_data.flags & torrent_flags::seed_mode);

	// there should not be any pieces in a seed-mode torrent
	auto const pieces = resume_data.have_pieces;
	for (bool const c : pieces)
	{
		TEST_CHECK(c);
	}
}


