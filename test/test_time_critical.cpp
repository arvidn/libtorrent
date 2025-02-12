/*

Copyright (c) 2014-2015, 2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "swarm_suite.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "test_utils.hpp"
#include "libtorrent/download_priority.hpp"
#include "libtorrent/torrent_info.hpp"

TORRENT_TEST(time_crititcal)
{
	test_swarm(test_flags::time_critical);
}

TORRENT_TEST(time_crititcal_zero_prio)
{
	lt::add_torrent_params atp = generate_torrent();

	lt::session ses(settings());

	atp.piece_priorities.resize(std::size_t(atp.ti->num_pieces()), lt::dont_download);
	atp.save_path = ".";
	auto h = ses.add_torrent(atp);

	wait_for_downloading(ses, "");

	h.set_piece_deadline(0_piece, 0, lt::torrent_handle::alert_when_available);
}

