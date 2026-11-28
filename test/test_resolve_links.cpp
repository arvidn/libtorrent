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
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"

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

	// these are all padded. The first small file is aligned but not tail
	// padded, so its piece hash also covers the following file
	{ "test2_pad_files", "test1_pad_files", 1},
	{ "test3_pad_files", "test1_pad_files", 1},
	{ "test3_pad_files", "test2_pad_files", 1},
	{ "test1_pad_files", "test2_pad_files", 1},
	{ "test1_pad_files", "test3_pad_files", 1},
	{ "test2_pad_files", "test3_pad_files", 1},

	// one might expect this to work, but since the tail of the single file
	// torrent is not padded, the last piece hash won't match
	{ "test1_pad_files", "test1_single", 0},

	// the last piece of the single file is short, while it's padded in
	// test1_pad_files. The same hash can't be correct for both
	{ "test1_pad_files", "test1_single_padded", 0},

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

namespace {

std::shared_ptr<torrent_info> make_v2_single_file(std::int64_t const size
	, sha256_hash const& root)
{
	file_storage fs;
	fs.add_file("test_resolve_links_dir/file", size);
	lt::create_torrent t(fs, 16 * 1024, lt::create_torrent::v2_only);
	// a file of a single piece has the piece hash as its root
	t.set_hash2(file_index_t{0}, piece_index_t::diff_type{0}, root);
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return std::make_shared<torrent_info>(buf, from_span);
}

std::shared_ptr<torrent_info> make_v1(std::vector<std::int64_t> const& sizes
	, bool const pad)
{
	file_storage fs;
	int idx = 0;
	for (auto const s : sizes)
		fs.add_file("test_resolve_links_dir/" + std::to_string(idx++), s);
	lt::create_torrent t(fs, 16 * 1024, lt::create_torrent::v1_only
		| (pad ? create_torrent::canonical_files : lt::create_flags_t{}));
	for (piece_index_t p : t.piece_range())
		t.set_hash(p, sha1_hash("01234567890123456789"));
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return std::make_shared<torrent_info>(buf, from_span);
}

std::shared_ptr<torrent_info> make_single_file(lt::create_flags_t const flags
	, sha1_hash const& hash, sha256_hash const& root)
{
	file_storage fs;
	fs.add_file("test_resolve_links_dir/file", 16 * 1024);
	lt::create_torrent t(fs, 16 * 1024, flags);
	if (!(flags & create_torrent::v2_only))
		t.set_hash(0_piece, hash);
	if (!(flags & create_torrent::v1_only))
		t.set_hash2(file_index_t{0}, piece_index_t::diff_type{0}, root);
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return std::make_shared<torrent_info>(buf, from_span);
}

int count_links(resolve_links const& l)
{
	auto const& links = l.get_links();
	return int(std::count_if(links.begin(), links.end()
		, std::bind(&resolve_links::link_t::ti, _1)));
}

}

// v2 files are matched by "pieces root" and file size. BEP 52 doesn't
// distinguish leaf hashes from interior nodes, so a file with different content
// may have the same root, but not the same size
TORRENT_TEST(v2_match_root_and_size)
{
	sha256_hash const root("01234567890123456789012345678901");
	auto const ti = make_v2_single_file(16 * 1024, root);

	{
		resolve_links l(ti);
		l.match(make_v2_single_file(16 * 1024, root), ".");
		TEST_EQUAL(count_links(l), 1);
	}

	{
		resolve_links l(ti);
		l.match(make_v2_single_file(64, root), ".");
		TEST_EQUAL(count_links(l), 0);
	}

	{
		resolve_links l(ti);
		l.match(make_v2_single_file(16 * 1024
			, sha256_hash("98765432109876543210987654321098")), ".");
		TEST_EQUAL(count_links(l), 0);
	}
}

TORRENT_TEST(v2_empty_file)
{
	auto const make = []
	{
		file_storage fs;
		fs.add_file("test_resolve_links_dir/a", 16 * 1024);
		fs.add_file("test_resolve_links_dir/b", 0);
		lt::create_torrent t(fs, 16 * 1024, lt::create_torrent::v2_only);
		t.set_hash2(file_index_t{0}, piece_index_t::diff_type{0}
			, sha256_hash("01234567890123456789012345678901"));
		std::vector<char> buf;
		bencode(std::back_inserter(buf), t.generate());
		return std::make_shared<torrent_info>(buf, from_span);
	};

	resolve_links l(make());
	l.match(make(), ".");
	// empty files aren't linked
	TEST_EQUAL(count_links(l), 1);
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
		resolve_links l(make_single_file(c.ours, hash, root));
		l.match(make_single_file(c.theirs, *c.their_hash, *c.their_root), ".");
		TEST_EQUAL(count_links(l), c.expected_links);
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
		resolve_links l(make_v1(c.ours, c.ours_padded));
		l.match(make_v1(c.theirs, c.theirs_padded), ".");
		TEST_EQUAL(count_links(l), c.expected_links);
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
			fs.add_file("t/" + std::to_string(idx++), f.size
				, f.pad ? file_storage::flag_pad_file : file_flags_t{});
		}
		fs.set_piece_length(16 * 1024);
		fs.set_num_pieces(aux::calc_num_pieces(fs));

		for (file_index_t const i : fs.file_range())
		{
			if (fs.pad_file_at(i)) continue;
			TEST_EQUAL(aux::piece_aligned(fs, i), files[std::size_t(static_cast<int>(i))].expected_aligned);
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

namespace {

// pad files are not stored on disk, so their path must not affect whether the
// files picked up from another torrent are found and checked
void test_pick_up_existing_file_pad_path(lt::disk_io_constructor_type const disk_io)
{
	lt::session_params sp(settings());
	sp.disk_io_constructor = disk_io;
	lt::session ses(std::move(sp));

	auto seed = make_test_torrent(torrent_args()
		.file("300001,name=a")
		.name("test-1")
		.multi_file());
	generate_files(*seed, ".");

	lt::add_torrent_params atp;
	atp.ti = seed;
	atp.save_path = ".";
	ses.add_torrent(atp);
	wait_for_seeding(ses, "add-seed");

	entry::list_type files;
	{
		// a path that can't be stat'ed
		entry f;
		f["attr"] = "p";
		f["length"] = 0;
		f["path"] = entry::list_type(21, entry(std::string(200, 'p')));
		files.push_back(std::move(f));
	}
	{
		entry f;
		f["length"] = seed->total_size();
		f["path"] = entry::list_type(1, entry("b"));
		files.push_back(std::move(f));
	}

	std::string pieces;
	for (auto const p : seed->piece_range())
		pieces += seed->hash_for_piece(p).to_string();

	entry torrent;
	entry& info = torrent["info"];
	info["name"] = "test-2";
	info["piece length"] = seed->piece_length();
	info["pieces"] = pieces;
	info["files"] = std::move(files);
	info["similar"] = entry::list_type(1
		, entry(seed->info_hashes().v1.to_string()));

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	atp = load_torrent_buffer(buf);
	atp.save_path = ".";
	torrent_handle const h = ses.add_torrent(atp);

	// the seed's torrent_checked_alert may still be queued
	for (;;)
	{
		alert const* a = wait_for_alert(ses, torrent_checked_alert::alert_type
			, "add-test-2", pop_alerts::cache_alerts);
		TEST_CHECK(a);
		if (a == nullptr) return;
		if (static_cast<torrent_alert const*>(a)->handle == h) break;
	}

	torrent_status const st = h.status();
	TEST_CHECK(st.is_seeding);
	TEST_EQUAL(st.num_pieces, seed->num_pieces());
}

} // anonymous namespace

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(pick_up_existing_file_pad_path_mmap)
{
	test_pick_up_existing_file_pad_path(lt::mmap_disk_io_constructor);
}
#endif

TORRENT_TEST(pick_up_existing_file_pad_path_posix)
{
	test_pick_up_existing_file_pad_path(lt::posix_disk_io_constructor);
}

#else
TORRENT_TEST(empty)
{
	TEST_CHECK(true);
}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS
