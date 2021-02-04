/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2015, Jakob Petsovits
Copyright (c) 2016, Eugene Shalygin
Copyright (c) 2017, Steven Siloti
Copyright (c) 2017-2018, Alden Torres
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
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/path.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "test.hpp"
#include "test_utils.hpp"
#include "settings.hpp"

#include <iostream>
#include <fstream>
#include <iostream>
#include <tuple>

using namespace lt;
using std::ignore;

namespace {

bool all_of(std::vector<bool> const& v)
{
	return std::all_of(v.begin(), v.end(), [](bool val){ return val; });
}

void test_remap_files(storage_mode_t storage_mode = storage_mode_sparse)
{
	using namespace lt;

	// create a torrent with 2 files, remap them into 3 files and make sure
	// the file priorities don't break things
	static std::array<const int, 2> const file_sizes{ {0x8000 * 2, 0x8000} };
	int const piece_size = 0x8000;
	auto t = make_torrent(file_sizes, piece_size);

	static std::array<const int, 2> const remap_file_sizes
		{{0x8000, 0x8000 * 2}};

	file_storage fs = make_file_storage(remap_file_sizes, piece_size, "multifile-");

	t->remap_files(fs);

	auto const alert_mask = alert_category::all
#if TORRENT_ABI_VERSION <= 2
		& ~alert_category::stats
#endif
	;

	session_proxy p1;

	settings_pack sett = settings();
	sett.set_int(settings_pack::alert_mask, alert_mask);
	lt::session ses(sett);

	add_torrent_params params;
	params.save_path = ".";
	params.storage_mode = storage_mode;
	params.flags &= ~torrent_flags::paused;
	params.flags &= ~torrent_flags::auto_managed;
	params.ti = t;

	torrent_handle tor1 = ses.add_torrent(params);

	// prevent race conditions of adding pieces while checking
	lt::torrent_status st = tor1.status();
	for (int i = 0; i < 40; ++i)
	{
		st = tor1.status();
		if (st.state != torrent_status::checking_files
			&& st.state != torrent_status::checking_resume_data)
			break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}
	TEST_CHECK(st.state != torrent_status::checking_files
		&& st.state != torrent_status::checking_files);
	TEST_CHECK(st.num_pieces == 0);

	// write pieces
	for (auto const i : fs.piece_range())
	{
		std::vector<char> const piece = generate_piece(i, fs.piece_size(i));
		tor1.add_piece(i, piece.data());
	}

	// read pieces
	for (auto const i : fs.piece_range())
	{
		tor1.read_piece(i);
	}

	// wait for all alerts to come back and verify the data against the expected
	// piece data
	aux::vector<bool, piece_index_t> pieces(std::size_t(fs.num_pieces()), false);
	aux::vector<bool, piece_index_t> passed(std::size_t(fs.num_pieces()), false);
	aux::vector<bool, file_index_t> files(std::size_t(fs.num_files()), false);

	while (!all_of(pieces) || !all_of(passed) || !all_of(files))
	{
		alert* a = ses.wait_for_alert(lt::seconds(5));
		if (a == nullptr) break;

		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);

		for (alert* i : alerts)
		{
			printf("%s\n", i->message().c_str());

			read_piece_alert* rp = alert_cast<read_piece_alert>(i);
			if (rp)
			{
				auto const idx = rp->piece;
				TEST_EQUAL(t->piece_size(idx), rp->size);

				std::vector<char> const piece = generate_piece(idx, t->piece_size(idx));
				TEST_CHECK(std::memcmp(rp->buffer.get(), piece.data(), std::size_t(rp->size)) == 0);
				TEST_CHECK(pieces[idx] == false);
				pieces[idx] = true;
			}

			file_completed_alert* fc = alert_cast<file_completed_alert>(i);
			if (fc)
			{
				auto const idx = fc->index;
				TEST_CHECK(files[idx] == false);
				files[idx] = true;
			}

			piece_finished_alert* pf = alert_cast<piece_finished_alert>(i);
			if (pf)
			{
				auto const idx = pf->piece_index;
				TEST_CHECK(passed[idx] == false);
				passed[idx] = true;
			}
		}
	}

	TEST_CHECK(all_of(pieces));
	TEST_CHECK(all_of(files));
	TEST_CHECK(all_of(passed));

	// just because we can read them back throught libtorrent, doesn't mean the
	// files have hit disk yet (because of the cache). Retry a few times to try
	// to pick up the files
	for (file_index_t i(0); i < file_index_t(int(remap_file_sizes.size())); ++i)
	{
		std::string name = fs.file_path(i);
		for (int j = 0; j < 10 && !exists(name); ++j)
		{
			std::this_thread::sleep_for(lt::milliseconds(500));
			print_alerts(ses, "ses");
		}

		std::printf("%s\n", name.c_str());
		TEST_CHECK(exists(name));
	}

	print_alerts(ses, "ses");

	st = tor1.status();
	TEST_EQUAL(st.is_seeding, true);

	std::printf("\ntesting force recheck\n\n");

	// test force rechecking a seeding torrent with remapped files
	tor1.force_recheck();

	for (int i = 0; i < 50; ++i)
	{
		torrent_status st1 = tor1.status();
		if (st1.is_seeding) break;
		std::this_thread::sleep_for(lt::milliseconds(100));
		print_alerts(ses, "ses");
	}

	print_alerts(ses, "ses");
	st = tor1.status();
	TEST_CHECK(st.is_seeding);
}

} // anonymous namespace

TORRENT_TEST(remap_files)
{
	test_remap_files();
}
