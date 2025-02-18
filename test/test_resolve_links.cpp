/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2015-2017, 2019-2022, Arvid Norberg
Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "test_utils.hpp"

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/resolve_links.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/load_torrent.hpp"

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
		std::shared_ptr<torrent_info const> ti1 = load_torrent_file(p).ti;

		p = combine_path(path, e.filename2) + ".torrent";
		std::printf("loading %s\n", p.c_str());
		std::shared_ptr<torrent_info const> ti2 = load_torrent_file(p).ti;

		std::printf("resolving\n");
		aux::resolve_links l(ti1);
		l.match(ti2, ".");

		aux::vector<aux::resolve_links::link_t, file_index_t> const& links = l.get_links();

		auto const num_matches = std::size_t(std::count_if(links.begin(), links.end()
			, std::bind(&aux::resolve_links::link_t::ti, _1)));

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
	std::vector<lt::create_file_entry> fs1;
	std::vector<lt::create_file_entry> fs2;

	fs1.emplace_back("test_resolve_links_dir/tmp1", 1024);
	fs1.emplace_back("test_resolve_links_dir/tmp2", 1024);
	fs2.emplace_back("test_resolve_links_dir/tmp1", 1024);
	fs2.emplace_back("test_resolve_links_dir/tmp2", 1024);

	lt::create_torrent t1(std::move(fs1), 1024, lt::create_torrent::v1_only);
	lt::create_torrent t2(std::move(fs2), 1024, lt::create_torrent::v1_only);

	t1.set_hash(0_piece, sha1_hash::max());
	t1.set_hash(1_piece, sha1_hash::max());
	t2.set_hash(0_piece, sha1_hash::max());
	t2.set_hash(1_piece, sha1_hash("01234567890123456789"));

	std::vector<char> const tmp1 = bencode(t1.generate());
	std::vector<char> const tmp2 = bencode(t2.generate());
	auto ti1 = load_torrent_buffer(tmp1).ti;
	auto ti2 = load_torrent_buffer(tmp2).ti;

	std::printf("resolving\n");
	aux::resolve_links l(ti1);
	l.match(ti2, ".");

	aux::vector<aux::resolve_links::link_t, file_index_t> const& links = l.get_links();

	auto const num_matches = std::count_if(links.begin(), links.end()
		, std::bind(&aux::resolve_links::link_t::ti, _1));

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
	auto atp = make_test_torrent(a);
	generate_files(*atp.ti, ".");

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
	atp = make_test_torrent(b);
	atp.save_path = ".";
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
