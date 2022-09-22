/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2015-2017, 2019-2021, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
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
#include "test_utils.hpp"

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/resolve_links.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/session.hpp"

#include "make_torrent.hpp"
#include "setup_transfer.hpp" // for wait_for_seeding
#include "settings.hpp"

#include <functional>

using namespace lt;
using namespace std::placeholders;

struct test_torrent_t
{
	char const* filename1;
	char const* filename2;
	std::string::size_type expected_matches;
};

static test_torrent_t test_torrents[] = {
	// no match because shared file in test2 and test3 is not padded/aligned
	{ "test2", "test1_pad_files", 0},
	{ "test3", "test1_pad_files", 0},

	// in this case, test1 happens to have the shared file as the first one,
	// which makes it padded, however, the tail of it isn't padded, so it
	// still overlaps with the next file
	{ "test1", "test1_pad_files", 0},

	// test2 and test3 don't have the shared file aligned
	{ "test2", "test1_pad_files", 0},
	{ "test3", "test1_pad_files", 0},
	{ "test2", "test1_single", 0},

	// these are all padded. The first small file will accidentally also
	// match, even though it's not tail padded, the following file is identical
	{ "test2_pad_files", "test1_pad_files", 2},
	{ "test3_pad_files", "test1_pad_files", 2},
	{ "test3_pad_files", "test2_pad_files", 2},
	{ "test1_pad_files", "test2_pad_files", 2},
	{ "test1_pad_files", "test3_pad_files", 2},
	{ "test2_pad_files", "test3_pad_files", 2},

	// one might expect this to work, but since the tail of the single file
	// torrent is not padded, the last piece hash won't match
	{ "test1_pad_files", "test1_single", 0},

	// if it's padded on the other hand, it will work
	{ "test1_pad_files", "test1_single_padded", 1},

	// TODO: test files with different piece size (negative test)
};

// TODO: it would be nice to test resolving of more than just 2 files as well.
// like 3 single file torrents merged into one, resolving all 3 files.

TORRENT_TEST(resolve_links)
{
	std::string path = combine_path(parent_path(current_working_directory())
		, "mutable_test_torrents");

	for (int i = 0; i < int(sizeof(test_torrents)/sizeof(test_torrents[0])); ++i)
	{
		test_torrent_t const& e = test_torrents[i];

		std::string p = combine_path(path, e.filename1) + ".torrent";
		std::printf("loading %s\n", p.c_str());
		std::shared_ptr<torrent_info> ti1 = std::make_shared<torrent_info>(p);

		p = combine_path(path, e.filename2) + ".torrent";
		std::printf("loading %s\n", p.c_str());
		std::shared_ptr<torrent_info> ti2 = std::make_shared<torrent_info>(p);

		std::printf("resolving\n");
		resolve_links l(ti1);
		l.match(ti2, ".");

		aux::vector<resolve_links::link_t, file_index_t> const& links = l.get_links();

		auto const num_matches = std::size_t(std::count_if(links.begin(), links.end()
			, std::bind(&resolve_links::link_t::ti, _1)));

		// some debug output in case the test fails
		if (num_matches > e.expected_matches)
		{
			file_storage const& fs = ti1->files();
			for (file_index_t idx{0}; idx != links.end_index(); ++idx)
			{
				TORRENT_ASSERT(idx < file_index_t{fs.num_files()});
				std::printf("%*s --> %s : %d\n"
					, int(fs.file_name(idx).size())
					, fs.file_name(idx).data()
					, links[idx].ti
					? aux::to_hex(links[idx].ti->info_hash()).c_str()
					: "", static_cast<int>(links[idx].file_idx));
			}
		}

		TEST_EQUAL(num_matches, e.expected_matches);

	}
}

// this ensure that internally there is a range lookup
// since the zero-hash piece is in the second place
TORRENT_TEST(range_lookup_duplicated_files)
{
	file_storage fs1;
	file_storage fs2;

	fs1.add_file("test_resolve_links_dir/tmp1", 1024);
	fs1.add_file("test_resolve_links_dir/tmp2", 1024);
	fs2.add_file("test_resolve_links_dir/tmp1", 1024);
	fs2.add_file("test_resolve_links_dir/tmp2", 1024);

	lt::create_torrent t1(fs1, 1024, lt::create_torrent::v1_only);
	lt::create_torrent t2(fs2, 1024, lt::create_torrent::v1_only);

	t1.set_hash(0_piece, sha1_hash::max());
	t1.set_hash(1_piece, sha1_hash::max());
	t2.set_hash(0_piece, sha1_hash::max());
	t2.set_hash(1_piece, sha1_hash("01234567890123456789"));

	std::vector<char> tmp1;
	std::vector<char> tmp2;
	bencode(std::back_inserter(tmp1), t1.generate());
	bencode(std::back_inserter(tmp2), t2.generate());
	auto ti1 = std::make_shared<torrent_info>(tmp1, from_span);
	auto ti2 = std::make_shared<torrent_info>(tmp2, from_span);

	std::printf("resolving\n");
	resolve_links l(ti1);
	l.match(ti2, ".");

	aux::vector<resolve_links::link_t, file_index_t> const& links = l.get_links();

	auto const num_matches = std::count_if(links.begin(), links.end()
		, std::bind(&resolve_links::link_t::ti, _1));

	TEST_EQUAL(num_matches, 1);
}

TORRENT_TEST(pick_up_existing_file)
{
	lt::session ses(settings());

	auto a = torrent_args()
		.file("34092,name=cruft-1")
		.file("31444,padfile")
		.file("9000000,name=dupliicate-file")
		.file("437184,padfile")
		.file("1348,name=cruft-2")
		.name("test-1")
		.collection("test-collection");
	auto seeding_torrent = make_test_torrent(a);
	generate_files(*seeding_torrent, ".");

	lt::add_torrent_params atp;
	atp.ti = seeding_torrent;
	atp.save_path = ".";
	ses.add_torrent(atp);

	wait_for_seeding(ses, "add-seed");

	auto b = torrent_args()
		.file("52346,name=cruft-3")
		.file("13190,padfile")
		.file("9000000,name=dupliicate-file-with-different-name")
		.file("437184,padfile")
		.file("40346,name=cruft-4")
		.name("test-2")
		.collection("test-collection");
	auto downloading_torrent = make_test_torrent(b);

	atp.ti = downloading_torrent;
	auto handle = ses.add_torrent(atp);

	wait_for_downloading(ses, "add-downloader");

	std::vector<std::int64_t> file_progress;
	handle.file_progress(file_progress);
	TEST_EQUAL(file_progress[2], 9000000);
}

#else
TORRENT_TEST(empty)
{
	TEST_CHECK(true);
}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS
