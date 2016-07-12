/*

Copyright (c) 2014, Arvid Norberg
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

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "setup_transfer.hpp"
#include "test.hpp"
#include "settings.hpp"

#include <iostream>
#include <fstream>
#include <iostream>
#include <tuple>

using namespace libtorrent;
namespace lt = libtorrent;
using std::ignore;

void test_remap_files(storage_mode_t storage_mode = storage_mode_sparse)
{
	using namespace libtorrent;

	// in case the previous run was terminated
	error_code ec;

	// create a torrent with 2 files, remap them into 3 files and make sure
	// the file priorities don't break things
	static const int file_sizes[] = {100000, 100000};
	const int num_files = sizeof(file_sizes)/sizeof(file_sizes[0]);
	const int piece_size = 0x8000;
	auto t = make_torrent(file_sizes, num_files, piece_size);

	int num_new_files = 3;

	static const int remap_file_sizes[] = {10000, 10000, 180000};
	file_storage fs = make_file_storage(rema_file_sizes, 3
		, t->piece_length(), "multifile-");

	t->remap_files(fs);

	int const alert_mask = alert::all_categories
		& ~alert::stats_notification;

	session_proxy p1;

	settings_pack sett = settings();
	sett.set_int(settings_pack::alert_mask, alert_mask);
	lt::session ses(sett);

	add_torrent_params params;
	params.save_path = ".";
	params.storage_mode = storage_mode;
	params.flags &= ~add_torrent_params::flag_paused;
	params.flags &= ~add_torrent_params::flag_auto_managed;
	params.ti = t;

	torrent_handle tor1 = ses.add_torrent(params);

	// write pieces
	for (int i = 0; i < fs.num_pieces(); ++i)
	{
		std::vector<char> piece = generate_piece(i, fs.piece_size(i));
		tor1.add_piece(i, piece.data());
	}

	// read pieces
	for (int i = 0; i < fs.num_pieces(); ++i)
	{
		tor1.read_piece(i);
	}

	// wait for all alerts to come back and verify the data against the expected
	// piece adata

	std::vector<bool> pieces(fs.num_pieces(), false);

	while (!std::all_of(pieces.begin(), pieces.end(), [](bool v){ return v; }))
	{
		alert* a = ses.wait_for_alert(lt::seconds(30));
		if (a == nullptr) break;

		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);

		for (alert* i : alerts)
		{
			printf("%s\n", i->message().c_str());
			read_piece_alert* rp = alert_cast<read_piece_alert>(i);
			if (rp == nullptr) continue;

			int const idx = rp->piece;
			TEST_EQUAL(t->piece_size(idx), rp->size);

			std::vector<char> const piece = generate_piece(idx, t->piece_size(idx));
			TEST_CHECK(memcmp(rp->buffer.get(), piece.data(), rp->size) == 0);
			TEST_CHECK(pieces[idx] == false);
			pieces[idx] = true;
		}
	}

	// just because we can read them back throught libtorrent, doesn't mean the
	// files have hit disk yet (because of the cache). Retry a few times to try
	// to pick up the files
	for (int i = 0; i < num_new_files; ++i)
	{
		std::string name = fs.file_path(i);
		for (int j = 0; j < 10 && !exists(name); ++j)
		{
			std::this_thread::sleep_for(lt::milliseconds(500));
			print_alerts(ses, "ses");
		}

		fprintf(stderr, "%s\n", name.c_str());
		TEST_CHECK(exists(name));
	}

	print_alerts(ses, "ses");

	torrent_status st = tor1.status();
	TEST_EQUAL(st.is_seeding, true);

	std::fprintf(stderr, "\ntesting force recheck\n\n");

	// test force rechecking a seeding torrent with remapped files
	tor1.force_recheck();

	for (int i = 0; i < 50; ++i)
	{
		torrent_status st = tor1.status();
		if (st.is_seeding) break;
		std::this_thread::sleep_for(lt::milliseconds(100));
		print_alerts(ses, "ses");
	}

	print_alerts(ses, "ses");
	st = tor1.status();
	TEST_CHECK(st.is_seeding);
}

TORRENT_TEST(remap_files)
{
	test_remap_files();
}

