/*

Copyright (c) 2014-2022, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/bitfield.hpp"
#include <tuple>
#include <functional>

#include "test.hpp"
#include "disk_io_test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"

#include <fstream>
#include <iostream>

using namespace lt;
using namespace std::chrono_literals;

TORRENT_TEST_DISK_IO(recheck)
{
	error_code ec;
	settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, test_listen_interface());
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_dht, false);
	lt::session_params sp(sett);
	sp.disk_io_constructor = disk_io;
	lt::session ses1(sp);
	create_directory("tmp1_recheck", ec);
	if (ec) std::printf("create_directory: %s\n", ec.message().c_str());
	std::ofstream file("tmp1_recheck/temporary");
	add_torrent_params param = ::create_torrent(&file, "temporary", 4 * 1024 * 1024
		, 7, false);
	file.close();

	param.flags &= ~torrent_flags::paused;
	param.flags &= ~torrent_flags::auto_managed;
	param.save_path = "tmp1_recheck";
	param.flags |= torrent_flags::seed_mode;
	torrent_handle tor1 = ses1.add_torrent(param, ec);
	if (ec) std::printf("add_torrent: %s\n", ec.message().c_str());

	wait_for_listen(ses1, "ses1");

	tor1.force_recheck();

	torrent_status st1 = tor1.status();
	TEST_CHECK(st1.progress_ppm <= 1000000);
	wait_for_complete(ses1, tor1, "ses1");

	tor1.force_recheck();

	st1 = tor1.status();
	TEST_CHECK(st1.progress_ppm <= 1000000);
	wait_for_complete(ses1, tor1, "ses1");
}

// Regression test for on_resume_data_checked() with resume data that pairs a
// truncated have_pieces bitfield (implying a previous full check was
// interrupted, and start_checking() will pick up checking from where it left
// off) with a non-empty unfinished_pieces entry at or past that point. That
// combination can't come from torrent::write_resume_data(): downloading (the
// only source of unfinished_pieces) is disabled for the whole duration of a
// check, and force_recheck() clears any pre-existing unfinished pieces from
// the picker before a check begins, so genuinely-produced resume data never
// has both at once. But add_torrent_params is a plain public struct, and
// resume data can come from disk, so this combination can still reach
// on_resume_data_checked() (as constructed by this test). Processing the
// unfinished_pieces entry in that case would dispatch a verify_piece() job
// for a piece start_checking() is also about to check, deterministically
// tripping disk_cache's "never two concurrent async_hash() requests for the
// same piece" invariant.
//
// on_resume_data_checked() skips the unfinished_pieces loop entirely
// whenever the have_pieces bitfield was truncated, rather than processing
// entries that start_checking() is about to redo anyway.
TORRENT_TEST_DISK_IO(recheck_unfinished_piece_vs_start_checking)
{
	error_code ec;
	settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, test_listen_interface());
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_dht, false);
	lt::session_params sp(sett);
	sp.disk_io_constructor = disk_io;
	lt::session ses1(sp);

	int const piece_size = default_block_size;
	int const num_pieces = 4;

	error_code ec2;
	create_directory("tmp1_recheck_unfinished", ec2);
	if (ec2)
		std::printf("create_directory: %s\n", ec2.message().c_str());

	// write the full (correct) payload to disk up front, so the resume data
	// below describes a torrent whose files are already complete on disk.
	std::ofstream file("tmp1_recheck_unfinished/temporary");
	add_torrent_params param = ::create_torrent(
		&file, "temporary", piece_size, num_pieces, false, lt::create_torrent::v1_only);
	file.close();

	param.flags &= ~torrent_flags::paused;
	param.flags &= ~torrent_flags::auto_managed;
	param.save_path = "tmp1_recheck_unfinished";

	// resume data claiming only piece 0 has been checked (a truncated
	// have_pieces bitfield, as if a previous full recheck was interrupted
	// right after piece 0). This makes on_resume_data_checked() resume
	// checking from piece 1 onward via start_checking().
	param.have_pieces.resize(1);
	param.have_pieces.set_bit(piece_index_t(0));

	// resume data *also* claims piece 1's only block is already written (but
	// not yet hash-checked). Genuine resume data would never pair this with
	// a truncated have_pieces bitfield (see the comment above), but nothing
	// stops an add_torrent_params constructed this way, directly or from an
	// external resume file, from reaching on_resume_data_checked().
	bitfield const blocks(1, true);
	param.unfinished_pieces[piece_index_t(1)] = blocks;

	torrent_handle tor1 = ses1.add_torrent(param, ec);
	if (ec)
		std::printf("add_torrent: %s\n", ec.message().c_str());

	wait_for_complete(ses1, tor1, "ses1");

	// all pieces are legitimately correct on disk, so start_checking()
	// covering pieces 1-3 (the unfinished_pieces entry for piece 1 is
	// skipped, per the comment above) should bring this to 100% on its own.
	std::vector<std::int64_t> const fp = tor1.file_progress();
	TEST_EQUAL(int(fp.size()), 1);
	TEST_EQUAL(fp[0], std::int64_t(piece_size) * num_pieces);
}
