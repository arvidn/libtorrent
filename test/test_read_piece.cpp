/*

Copyright (c) 2013, 2015-2021, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/path.hpp"

namespace {

enum flags_t
{
	seed_mode = 1,
	time_critical = 2
};

void test_read_piece(int flags)
{
	using namespace lt;

	std::printf("==== TEST READ PIECE =====\n");

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_read_piece", ec);
	if (ec) std::printf("ERROR: removing tmp1_read_piece: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	create_directory("tmp1_read_piece", ec);
	if (ec) std::printf("ERROR: creating directory tmp1_read_piece: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	create_directory(combine_path("tmp1_read_piece", "test_torrent"), ec);
	if (ec) std::printf("ERROR: creating directory test_torrent: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	int piece_size = 0x4000;

	static std::array<const int, 2> const file_sizes{{ 100000, 10000 }};

	create_random_files(combine_path("tmp1_read_piece", "test_torrent"), file_sizes);

	auto fs = list_files(combine_path("tmp1_read_piece", "test_torrent"));
	lt::create_torrent t(std::move(fs), piece_size);

	// calculate the hash for all pieces
	set_piece_hashes(t, "tmp1_read_piece", ec);
	if (ec) std::printf("ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto ti = std::make_shared<torrent_info>(buf, ec, from_span);

	std::printf("generated torrent: %s tmp1_read_piece/test_torrent\n"
		, aux::to_hex(ti->info_hashes().v1).c_str());

	settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses(sett);

	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.save_path = "tmp1_read_piece";
	p.ti = ti;
	if (flags & flags_t::seed_mode)
		p.flags |= torrent_flags::seed_mode;
	torrent_handle tor1 = ses.add_torrent(p, ec);
	if (ec) std::printf("ERROR: add_torrent: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	TEST_CHECK(!ec);
	TEST_CHECK(tor1.is_valid());

	alert const* a = wait_for_alert(ses, torrent_finished_alert::alert_type, "ses");
	TEST_CHECK(a);

	TEST_CHECK(tor1.status().is_seeding);

	if (flags & time_critical)
	{
		tor1.set_piece_deadline(1_piece, 0, torrent_handle::alert_when_available);
	}
	else
	{
		tor1.read_piece(1_piece);
	}

	a = wait_for_alert(ses, read_piece_alert::alert_type, "ses");

	TEST_CHECK(a);
	if (a)
	{
		read_piece_alert const* rp = alert_cast<read_piece_alert>(a);
		TEST_CHECK(rp);
		if (rp)
		{
			TEST_EQUAL(rp->piece, 1_piece);
		}
	}

	remove_all("tmp1_read_piece", ec);
	if (ec) std::printf("ERROR: removing tmp1_read_piece: (%d) %s\n"
		, ec.value(), ec.message().c_str());
}

} // anonymous namespace

TORRENT_TEST(read_piece)
{
	test_read_piece(0);
}

TORRENT_TEST(seed_mode)
{
	test_read_piece(seed_mode);
}

TORRENT_TEST(time_critical)
{
	test_read_piece(time_critical);
}
