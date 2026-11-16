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

#include <array>
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
	{"test2", "test1_pad_files", 0},
	{"test3", "test1_pad_files", 0},

	// in this case, test1 happens to have the shared file as the first one,
	// which makes it padded, however, the tail of it isn't padded, so it
	// still overlaps with the next file
	{"test1", "test1_pad_files", 0},

	// test2 and test3 don't have the shared file aligned
	{"test2", "test1_pad_files", 0},
	{"test3", "test1_pad_files", 0},
	{"test2", "test1_single", 0},

	// these are all padded. The first small file is aligned but not tail
	// padded, so its piece hash also covers the following file
	{"test2_pad_files", "test1_pad_files", 1},
	{"test3_pad_files", "test1_pad_files", 1},
	{"test3_pad_files", "test2_pad_files", 1},
	{"test1_pad_files", "test2_pad_files", 1},
	{"test1_pad_files", "test3_pad_files", 1},
	{"test2_pad_files", "test3_pad_files", 1},

	// one might expect this to work, but since the tail of the single file
	// torrent is not padded, the last piece hash won't match
	{"test1_pad_files", "test1_single", 0},

	// the last piece of the single file is short, while it's padded in
	// test1_pad_files. The same hash can't be correct for both
	{"test1_pad_files", "test1_single_padded", 0},

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
		auto atp2 = load_torrent_file(p);
		renamed_files rf;
		rf.import_filenames(atp2.ti->layout(), atp2.renamed_files);

		std::printf("resolving\n");
		aux::resolve_links l(ti1);
		l.match(*atp2.ti, filenames(atp2.ti->layout(), rf), ".");

		aux::vector<std::string, file_index_t> const& links = l.get_links();

		auto const num_matches = std::size_t(std::count_if(links.begin(), links.end()
			, [](std::string const& link) { return !link.empty(); }));

		// some debug output in case the test fails
		if (num_matches > e.expected_matches)
		{
			file_storage const& fs = ti1->layout();
			for (file_index_t idx{0}; idx != links.end_index(); ++idx)
			{
				TORRENT_ASSERT(idx < file_index_t{fs.num_files()});
				std::printf("%*s --> %s\n"
					, int(fs.file_name(idx).size())
					, fs.file_name(idx).data()
					, links[idx].c_str());
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
	auto atp2 = load_torrent_buffer(tmp2);
	renamed_files rf;
	rf.import_filenames(atp2.ti->layout(), atp2.renamed_files);

	std::printf("resolving\n");
	aux::resolve_links l(ti1);
	l.match(*atp2.ti, filenames(atp2.ti->layout(), rf), ".");

	aux::vector<std::string, file_index_t> const& links = l.get_links();

	auto const num_matches = std::size_t(std::count_if(links.begin(), links.end()
		, [](std::string const& link) { return !link.empty(); }));

	TEST_EQUAL(num_matches, 1);
}

namespace {

std::shared_ptr<torrent_info const> make_v2_single_file(
	std::int64_t const size, sha256_hash const& root)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_resolve_links_dir/file", size);
	lt::create_torrent t(std::move(fs), 16 * 1024, lt::create_torrent::v2_only);
	// a file of a single piece has the piece hash as its root
	t.set_hash2(file_index_t{0}, piece_index_t::diff_type{0}, root);
	return load_torrent_buffer(bencode(t.generate())).ti;
}

std::shared_ptr<torrent_info const> make_v1(std::vector<std::int64_t> const& sizes, bool const pad)
{
	std::vector<lt::create_file_entry> fs;
	int idx = 0;
	for (auto const s : sizes)
		fs.emplace_back("test_resolve_links_dir/" + std::to_string(idx++), s);
	lt::create_torrent t(std::move(fs),
		16 * 1024,
		lt::create_torrent::v1_only
			| (pad ? create_torrent::canonical_files : lt::create_flags_t{}));
	for (piece_index_t p : t.piece_range())
		t.set_hash(p, sha1_hash("01234567890123456789"));
	return load_torrent_buffer(bencode(t.generate())).ti;
}

std::shared_ptr<torrent_info const> make_single_file(
	lt::create_flags_t const flags, sha1_hash const& hash, sha256_hash const& root)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_resolve_links_dir/file", 16 * 1024);
	lt::create_torrent t(std::move(fs), 16 * 1024, flags);
	if (!(flags & create_torrent::v2_only))
		t.set_hash(0_piece, hash);
	if (!(flags & create_torrent::v1_only))
		t.set_hash2(file_index_t{0}, piece_index_t::diff_type{0}, root);
	return load_torrent_buffer(bencode(t.generate())).ti;
}

int count_links(aux::resolve_links const& l)
{
	auto const& links = l.get_links();
	return int(std::count_if(
		links.begin(), links.end(), [](std::string const& link) { return !link.empty(); }));
}

int match_links(
	std::shared_ptr<torrent_info const> const& ti, std::shared_ptr<torrent_info const> const& other)
{
	aux::resolve_links l(ti);
	renamed_files rf;
	l.match(*other, filenames(other->layout(), rf), ".");
	return count_links(l);
}

}

// v2 files are matched by "pieces root" and file size. BEP 52 doesn't
// distinguish leaf hashes from interior nodes, so a file with different content
// may have the same root, but not the same size
TORRENT_TEST(v2_match_root_and_size)
{
	sha256_hash const root("01234567890123456789012345678901");
	auto const ti = make_v2_single_file(16 * 1024, root);

	TEST_EQUAL(match_links(ti, make_v2_single_file(16 * 1024, root)), 1);
	TEST_EQUAL(match_links(ti, make_v2_single_file(64, root)), 0);
	TEST_EQUAL(match_links(ti,
				   make_v2_single_file(16 * 1024, sha256_hash("98765432109876543210987654321098"))),
		0);
}

TORRENT_TEST(v2_empty_file)
{
	auto const make = [] {
		std::vector<lt::create_file_entry> fs;
		fs.emplace_back("test_resolve_links_dir/a", 16 * 1024);
		fs.emplace_back("test_resolve_links_dir/b", 0);
		lt::create_torrent t(std::move(fs), 16 * 1024, lt::create_torrent::v2_only);
		t.set_hash2(file_index_t{0},
			piece_index_t::diff_type{0},
			sha256_hash("01234567890123456789012345678901"));
		return load_torrent_buffer(bencode(t.generate())).ti;
	};

	// empty files aren't linked
	TEST_EQUAL(match_links(make(), make()), 1);
}

// every kind of hash the new torrent has must be compared against the seed
TORRENT_TEST(match_torrent_kind)
{
	lt::create_flags_t const v1 = create_torrent::v1_only;
	lt::create_flags_t const v2 = create_torrent::v2_only;
	lt::create_flags_t const hybrid{};
	sha1_hash const hash("01234567890123456789");
	sha1_hash const other_hash("98765432109876543210");
	sha256_hash const root("01234567890123456789012345678901");
	sha256_hash const other_root("98765432109876543210987654321098");

	struct test_case_t
	{
		lt::create_flags_t ours;
		lt::create_flags_t theirs;
		sha1_hash const* their_hash;
		sha256_hash const* their_root;
		int expected_links;
	};

	std::array<test_case_t, 11> const test_cases{{
		{v1, v1, &hash, &root, 1},
		{v2, v2, &hash, &root, 1},
		{hybrid, hybrid, &hash, &root, 1},
		{hybrid, hybrid, &other_hash, &root, 0},
		{hybrid, hybrid, &hash, &other_root, 0},
		{hybrid, v1, &hash, &root, 0},
		{hybrid, v2, &hash, &root, 0},
		{v1, hybrid, &hash, &root, 1},
		{v2, hybrid, &hash, &root, 1},
		{v1, v2, &hash, &root, 0},
		{v2, v1, &hash, &root, 0},
	}};

	for (auto const& c : test_cases)
	{
		TEST_EQUAL(match_links(make_single_file(c.ours, hash, root),
					   make_single_file(c.theirs, *c.their_hash, *c.their_root)),
			c.expected_links);
	}
}

// the last piece of a v1 file that isn't followed by a pad file also covers
// the next file, so its hash doesn't establish that the file is identical
TORRENT_TEST(v1_tail_padding)
{
	struct test_case_t
	{
		std::vector<std::int64_t> ours;
		bool ours_padded;
		std::vector<std::int64_t> theirs;
		bool theirs_padded;
		int expected_links;
	};

	// all piece hashes are identical, only the layout determines the outcome
	std::array<test_case_t, 8> const test_cases{{
		// the second file isn't piece aligned
		{{20000, 1000}, false, {20000, 1000}, false, 0},
		{{20000, 1000}, true, {20000, 1000}, true, 2},
		// the last piece is short in one torrent and padded in the other
		{{20000}, false, {20000, 1000}, true, 0},
		{{20000, 1000}, true, {20000}, false, 0},
		{{20000}, false, {20000}, false, 1},
		// a file that ends on a piece boundary doesn't need a pad file
		{{32768, 1000}, false, {32768, 1000}, false, 2},
		{{32768, 1000, 1000}, false, {32768, 1000, 1000}, false, 1},
		// empty files aren't linked
		{{32768, 0}, false, {32768, 0}, false, 1},
	}};

	for (auto const& c : test_cases)
	{
		TEST_EQUAL(match_links(make_v1(c.ours, c.ours_padded), make_v1(c.theirs, c.theirs_padded)),
			c.expected_links);
	}
}

TORRENT_TEST(piece_aligned)
{
	struct file_t
	{
		std::int64_t size;
		bool pad;
		// ignored for pad files
		bool expected_aligned;
	};

	std::array<std::vector<file_t>, 7> const test_cases{{
		{{20000, false, true}},
		{{20000, false, false}, {1000, false, false}},
		{{20000, false, true}, {12768, true, false}, {1000, false, true}},
		{{16384, false, true}, {1000, false, true}},
		// the pad file doesn't reach the piece boundary
		{{20000, false, false}, {1000, true, false}, {1000, false, false}},
		{{20000, false, true}, {12768, true, false}},
		// split pad files aren't supported
		{{20000, false, false}, {6384, true, false}, {6384, true, false}, {1000, false, true}},
	}};

	for (auto const& files : test_cases)
	{
		file_storage fs;
		int idx = 0;
		for (auto const& f : files)
		{
			fs.add_file("t/" + std::to_string(idx++),
				f.size,
				f.pad ? file_storage::flag_pad_file : file_flags_t{});
		}
		fs.set_piece_length(16 * 1024);
		fs.set_num_pieces(aux::calc_num_pieces(fs));

		for (file_index_t const i : fs.file_range())
		{
			if (fs.pad_file_at(i))
				continue;
			TEST_EQUAL(aux::piece_aligned(fs, i),
				files[std::size_t(static_cast<int>(i))].expected_aligned);
		}
	}
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
