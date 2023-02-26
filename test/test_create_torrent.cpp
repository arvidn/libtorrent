/*

Copyright (c) 2016, 2021, Alden Torres
Copyright (c) 2016-2022, Arvid Norberg
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2017-2018, Steven Siloti
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
#include "setup_transfer.hpp"

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/units.hpp"

#include <cstring>
#include <iostream>
#include <fstream>
#include <string>

using namespace std::literals::string_literals;

// make sure creating a torrent from an existing handle preserves the
// info-dictionary verbatim, so as to not alter the info-hash
TORRENT_TEST(create_verbatim_torrent)
{
	char const test_torrent[] = "d4:infod4:name6:foobar6:lengthi12345e"
		"12:piece lengthi65536e6:pieces20:ababababababababababee";

	lt::torrent_info info(test_torrent, lt::from_span);
	lt::create_torrent t(info);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());

	TEST_CHECK(buffer == t.generate_buf());

	// now, make sure the info dictionary was unchanged
	buffer.push_back('\0');
	char const* dest_info = std::strstr(&buffer[0], "4:info");

	TEST_CHECK(dest_info != nullptr);

	// +1 and -2 here is to strip the outermost dictionary from the source
	// torrent, since create_torrent may have added items next to the info dict
	TEST_CHECK(memcmp(dest_info, test_torrent + 1, sizeof(test_torrent)-3) == 0);
}

TORRENT_TEST(auto_piece_size)
{
	std::int64_t const kiB = 1024;
	std::int64_t const MiB = 1024 * 1024;
	std::int64_t const GiB = 1024 * 1024 * 1024;
	std::array<std::pair<std::int64_t, std::int64_t>, 11> samples{{
		{100LL,     16 * kiB},
		{3 * MiB,   32 * kiB},
		{11 * MiB,  64 * kiB},
		{43 * MiB,  128 * kiB},
		{172 * MiB, 256 * kiB},
		{688 * MiB, 512 * kiB},
		{3 * GiB,   1 * MiB},
		{11 * GiB,  2 * MiB},
		{44 * GiB,  4 * MiB},
		{176 * GiB, 8 * MiB},
		{704 * GiB, 16 * MiB},
	}};

	for (auto const& t : samples)
	{
		lt::file_storage fs;
		fs.add_file("a", t.first);
		lt::create_torrent ct(fs, 0);
		TEST_CHECK(ct.piece_length() == static_cast<int>(t.second));
	}
}

namespace {
int test_piece_size(int const piece_size, lt::create_flags_t const f = {})
{
	std::int64_t const MiB = 1024 * 1024;
	lt::file_storage fs;
	fs.add_file("a", 100 * MiB);
	lt::create_torrent ct(fs, piece_size, f);
	return ct.piece_length();
}
}

TORRENT_TEST(piece_size_restriction_16kB)
{
	TEST_EQUAL(test_piece_size(15000), 16 * 1024);
	TEST_EQUAL(test_piece_size(500), 16 * 1024);
	TEST_THROW(test_piece_size(15000, lt::create_torrent::v1_only));
	TEST_THROW(test_piece_size(8000, lt::create_torrent::v1_only));
	TEST_EQUAL(test_piece_size(8192, lt::create_torrent::v1_only), 8192);
}

TORRENT_TEST(piece_size_quanta)
{
	TEST_EQUAL(test_piece_size(32 * 1024), 32 * 1024);
	TEST_EQUAL(test_piece_size(32 * 1024, lt::create_torrent::v1_only), 32 * 1024);
	TEST_THROW(test_piece_size(48 * 1024));
	TEST_EQUAL(test_piece_size(48 * 1024, lt::create_torrent::v1_only), 48 * 1024);
	TEST_THROW(test_piece_size(47 * 1024, lt::create_torrent::v1_only));
	TEST_THROW(test_piece_size(47 * 1024));
}

TORRENT_TEST(create_torrent_round_trip)
{
	char const test_torrent[] = "d8:announce26:udp://testurl.com/announce7:comment22:this is a test comment13:creation datei1337e4:infod6:lengthi12345e4:name6:foobar12:piece lengthi65536e6:pieces20:ababababababababababee";
	lt::torrent_info info1(test_torrent, lt::from_span);
	TEST_EQUAL(info1.comment(), "this is a test comment");
	TEST_EQUAL(info1.trackers().size(), 1);
	TEST_EQUAL(info1.trackers().front().url, "udp://testurl.com/announce");

	lt::create_torrent t(info1);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	TEST_CHECK(buffer == t.generate_buf());
	lt::torrent_info info2(buffer, lt::from_span);

	TEST_EQUAL(info2.comment(), "this is a test comment");
	TEST_EQUAL(info2.trackers().size(), 1);
	TEST_EQUAL(info2.trackers().front().url, "udp://testurl.com/announce");
	TEST_CHECK(info1.info_hashes() == info2.info_hashes());
	TEST_CHECK(info2.hash_for_piece(0_piece) == info1.hash_for_piece(0_piece));
}

namespace {

void test_round_trip_torrent(std::string const& name)
{
	std::string const root_dir = lt::parent_path(lt::current_working_directory());
	std::string const filename = lt::combine_path(lt::combine_path(root_dir, "test_torrents"), name);
	std::vector<char> v2_buffer;
	lt::error_code ec;
	int const ret = load_file(filename, v2_buffer, ec);
	TEST_CHECK(ret == 0);
	TEST_CHECK(!ec);

	lt::bdecode_node in_torrent = lt::bdecode(v2_buffer);

	lt::torrent_info info1(v2_buffer, lt::from_span);
	lt::create_torrent t(info1);

	std::vector<char> out_buffer;
	lt::entry e = t.generate();
	lt::bencode(std::back_inserter(out_buffer), e);
	TEST_CHECK(out_buffer == t.generate_buf());

	lt::bdecode_node out_torrent = lt::bdecode(out_buffer);

	TEST_CHECK(out_torrent.dict_find("info").data_section()
		== in_torrent.dict_find("info").data_section());

	auto in_piece_layers = in_torrent.dict_find("piece layers").data_section();
	auto out_piece_layers = out_torrent.dict_find("piece layers").data_section();
	TEST_CHECK(out_piece_layers == in_piece_layers);
}

}

TORRENT_TEST(create_torrent_round_trip_v2)
{
	test_round_trip_torrent("v2_only.torrent");
}

TORRENT_TEST(create_torrent_round_trip_hybrid_missing_tailpad)
{
	test_round_trip_torrent("v2_hybrid-missing-tailpad.torrent");
}

TORRENT_TEST(create_torrent_round_trip_hybrid)
{
	test_round_trip_torrent("v2_hybrid.torrent");
}

TORRENT_TEST(create_torrent_round_trip_empty_file)
{
	test_round_trip_torrent("v2_empty_file.torrent");
}

// check that attempting to create a torrent containing both
// a file and directory with the same name is not allowed
TORRENT_TEST(v2_path_conflict)
{
	lt::file_storage fs;

	for (int i = 0; i < 2; ++i)
	{
		switch (i)
		{
		case 0:
			fs.add_file("test/A/tmp", 0x4000);
			fs.add_file("test/a", 0x4000);
			fs.add_file("test/A", 0x4000);
			fs.add_file("test/filler", 0x4000);
			break;
		case 1:
			fs.add_file("test/long/path/name/that/collides", 0x4000);
			fs.add_file("test/long/path", 0x4000);
			fs.add_file("test/filler-1", 0x4000);
			fs.add_file("test/filler-2", 0x4000);
			break;
		}

		lt::create_torrent t(fs, 0x4000);
		lt::sha256_hash const dummy("01234567890123456789012345678901");
		lt::piece_index_t::diff_type zero(0);
		t.set_hash2(0_file, zero, dummy);
		t.set_hash2(1_file, zero, dummy);
		t.set_hash2(2_file, zero, dummy);
		t.set_hash2(3_file, zero, dummy);
		TEST_THROW(t.generate());
		TEST_THROW(t.generate_buf());
	}
}

TORRENT_TEST(v2_only)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	fs.add_file("test/B", 0x4002);
	lt::create_torrent t(fs, 0x4000, lt::create_torrent::v2_only);

	using p = lt::piece_index_t::diff_type;
	t.set_hash2(0_file, p(0), lt::sha256_hash::max());
	t.set_hash2(0_file, p(1), lt::sha256_hash::max());
	t.set_hash2(0_file, p(2), lt::sha256_hash::max());
	// file 1 is a pad file
	t.set_hash2(2_file, p(0), lt::sha256_hash::max());
	t.set_hash2(2_file, p(1), lt::sha256_hash::max());

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	TEST_CHECK(buffer == t.generate_buf());
	lt::torrent_info info(buffer, lt::from_span);
	TEST_CHECK(info.info_hashes().has_v2());
	TEST_CHECK(!info.info_hashes().has_v1());
	TEST_EQUAL(info.files().file_name(0_file), "A");
	TEST_CHECK(info.files().pad_file_at(1_file));
	TEST_EQUAL(info.files().file_name(2_file), "B");
	TEST_EQUAL(info.name(), "test");

	lt::create_torrent t2(info);
	std::vector<char> buffer2;
	lt::bencode(std::back_inserter(buffer2), t2.generate());
	TEST_CHECK(buffer2 == t2.generate_buf());

	TEST_CHECK(buffer == buffer2);
}

TORRENT_TEST(v2_only_set_hash)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	lt::create_torrent t(fs, 0x4000, lt::create_torrent::v2_only);

	TEST_THROW(t.set_hash(0_piece, lt::sha1_hash::max()));
}

#if TORRENT_HAS_SYMLINK || !defined TORRENT_WINDOWS
namespace {

void check(int ret)
{
	if (ret == 0) return;
	lt::error_code ec(errno, lt::generic_category());
	std::cerr << "call failed: " << ec.message() << '\n';
	throw std::runtime_error(ec.message());
}

}
#endif

#if TORRENT_HAS_SYMLINK

TORRENT_TEST(create_torrent_symlink)
{
	lt::error_code ec;
	lt::create_directories("test-torrent/a/b/c", ec);
	lt::create_directories("test-torrent/d/", ec);
	std::ofstream f1("test-torrent/a/b/c/file-1");
	check(::truncate("test-torrent/a/b/c/file-1", 1000));
	std::ofstream f2("test-torrent/d/file-2");
	check(::truncate("test-torrent/d/file-2", 1000));
	check(::symlink("../a/b/c/file-1", "test-torrent/d/test-link-1"));
	check(::symlink("a/b/c/file-1", "test-torrent/test-link-2"));
	check(::symlink("a/b/c/file-1", "test-torrent/a/b/c/test-link-3"));
	check(::symlink("../../../d/file-2", "test-torrent/a/b/c/test-link-4"));

	lt::file_storage fs;
	lt::add_files(fs, "test-torrent"
		, [](std::string n){ std::cout << n << '\n'; return true; }, lt::create_torrent::symlinks);

	lt::create_torrent t(fs, 16 * 1024, lt::create_torrent::symlinks);
	lt::set_piece_hashes(t, ".", [] (lt::piece_index_t) {});

	std::vector<char> torrent;
	lt::bencode(back_inserter(torrent), t.generate());
	TEST_CHECK(torrent == t.generate_buf());

	lt::torrent_info ti(torrent, lt::from_span);

	int found = 0;
	for (auto i : ti.files().file_range())
	{
		auto const filename = ti.files().file_path(i);

		if (filename == "test-torrent/d/test-link-1"
			|| filename == "test-torrent/test-link-2"
			|| filename == "test-torrent/a/b/c/test-link-3")
		{
			TEST_EQUAL(ti.files().symlink(i), "test-torrent/a/b/c/file-1");
			++found;
		}
		else if (filename == "test-torrent/a/b/c/test-link-4")
		{
			TEST_EQUAL(ti.files().symlink(i), "test-torrent/d/file-2");
			++found;
		}
	}
	TEST_EQUAL(found, 4);
}

#endif

#ifndef TORRENT_WINDOWS

TORRENT_TEST(v2_attributes)
{
	std::ofstream f1("file-1");
	check(::truncate("file-1", 1000));
	check(::chmod("file-1", S_IWUSR | S_IRUSR | S_IXUSR));

	lt::file_storage fs;
	lt::add_files(fs, "file-1", [](std::string){ return true; }, {});

	lt::create_torrent t(fs, 16 * 1024, {});
	lt::set_piece_hashes(t, ".", [] (lt::piece_index_t) {});

	lt::entry e = t.generate();

	std::cout << e.to_string() << '\n';

	TEST_EQUAL(e["info"]["attr"].string(), "x");
	TEST_EQUAL(e["info"]["file tree"]["file-1"][""]["attr"].string(), "x");
}
#endif

TORRENT_TEST(v1_only_set_hash2)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	lt::create_torrent t(fs, 0x4000, lt::create_torrent::v1_only);

	using p = lt::piece_index_t::diff_type;
	TEST_THROW(t.set_hash2(0_file, p(0), lt::sha256_hash::max()));
}

// if we don't specify a v2-only flag, but only set v2 hashes, the created
// torrent is implicitly v2-only
TORRENT_TEST(implicit_v2_only)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	fs.add_file("test/B", 0x4002);
	lt::create_torrent t(fs, 0x4000);

	using p = lt::piece_index_t::diff_type;
	t.set_hash2(0_file, p(0), lt::sha256_hash::max());
	t.set_hash2(0_file, p(1), lt::sha256_hash::max());
	t.set_hash2(0_file, p(2), lt::sha256_hash::max());
	// file 1 is a pad file
	t.set_hash2(2_file, p(0), lt::sha256_hash::max());
	t.set_hash2(2_file, p(1), lt::sha256_hash::max());

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	TEST_CHECK(buffer == t.generate_buf());
	lt::torrent_info info(buffer, lt::from_span);
	TEST_CHECK(info.info_hashes().has_v2());
	TEST_CHECK(!info.info_hashes().has_v1());
	TEST_EQUAL(info.files().file_name(0_file), "A");
	TEST_EQUAL(info.files().pad_file_at(1_file), true);
	TEST_EQUAL(info.files().file_name(2_file), "B");
	TEST_EQUAL(info.name(), "test");
}

// if we don't specify a v1-only flag, but only set v1 hashes, the created
// torrent is implicitly v1-only
TORRENT_TEST(implicit_v1_only)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	fs.add_file("test/B", 0x4002);
	lt::create_torrent t(fs, 0x4000);

	for (lt::piece_index_t i : fs.piece_range())
		t.set_hash(i, lt::sha1_hash::max());

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	TEST_CHECK(buffer == t.generate_buf());
	lt::torrent_info info(buffer, lt::from_span);
	TEST_CHECK(!info.info_hashes().has_v2());
	TEST_CHECK(info.info_hashes().has_v1());
	TEST_EQUAL(info.files().file_name(0_file), "A");
	TEST_EQUAL(info.files().pad_file_at(1_file), true);
	TEST_EQUAL(info.files().file_name(2_file), "B");
	TEST_EQUAL(info.name(), "test");
}

namespace {

template <typename Fun>
lt::torrent_info test_field(Fun f)
{
	lt::file_storage fs;
	fs.add_file("A", 0x4000);
	lt::create_torrent t(fs, 0x4000);
	for (lt::piece_index_t i : fs.piece_range())
		t.set_hash(i, lt::sha1_hash::max());

	f(t);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	TEST_CHECK(buffer == t.generate_buf());
	return lt::torrent_info(buffer, lt::from_span);
}
}

TORRENT_TEST(no_creation_date)
{
	auto info = test_field([](lt::create_torrent& t){
		t.set_creation_date(0);
	});
	TEST_EQUAL(info.creation_date(), 0);
}

TORRENT_TEST(creation_date)
{
	auto info = test_field([](lt::create_torrent& t){
		t.set_creation_date(1337);
	});
	TEST_EQUAL(info.creation_date(), 1337);
}

TORRENT_TEST(comment)
{
	auto info = test_field([](lt::create_torrent& t){
		t.set_comment("foobar");
	});
	TEST_EQUAL(info.comment(), "foobar");
}

TORRENT_TEST(creator)
{
	auto info = test_field([](lt::create_torrent& t){
		t.set_creator("foobar");
	});
	TEST_EQUAL(info.creator(), "foobar");
}

TORRENT_TEST(dht_nodes)
{
	auto info = test_field([](lt::create_torrent& t){
		t.add_node({"foobar"s, 1337});
	});
	using nodes = std::vector<std::pair<std::string, int>>;
	TEST_CHECK((info.nodes() == nodes{{"foobar", 1337}}));
}

TORRENT_TEST(ssl_cert)
{
	auto info = test_field([](lt::create_torrent& t){
		t.set_root_cert("foobar");
	});
	TEST_EQUAL(info.ssl_cert(), "foobar");
}

TORRENT_TEST(priv)
{
	auto info = test_field([](lt::create_torrent& t){
		t.set_priv(true);
	});
	TEST_CHECK(info.priv());
}

TORRENT_TEST(piece_layer)
{
	lt::file_storage fs;
	fs.add_file("test/large", 0x8000);
	fs.add_file("test/small-1", 0x4000);
	fs.add_file("test/small-2", 0x3fff);
	lt::create_torrent t(fs, 0x4000);

	using p = lt::piece_index_t::diff_type;
	t.set_hash2(0_file, p(0), lt::sha256_hash::max());
	t.set_hash2(0_file, p(1), lt::sha256_hash::max());
	t.set_hash2(1_file, p(0), lt::sha256_hash::max());
	t.set_hash2(2_file, p(0), lt::sha256_hash::max());

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	TEST_CHECK(buffer == t.generate_buf());
	lt::torrent_info info(buffer, lt::from_span);

	TEST_CHECK(info.piece_layer(0_file).size() == lt::sha256_hash::size() * 2);
	TEST_CHECK(info.piece_layer(1_file).size() == lt::sha256_hash::size());
	TEST_CHECK(info.piece_layer(2_file).size() == lt::sha256_hash::size());
}

TORRENT_TEST(pieces_root_empty_file)
{
	lt::file_storage fs;
	fs.add_file("test/1-empty", 0);
	fs.add_file("test/2-small", 0x3fff);
	fs.add_file("test/3-empty", 0);
	lt::create_torrent t(fs, 0x4000);

	using p = lt::piece_index_t::diff_type;
	t.set_hash2(1_file, p(0), lt::sha256_hash::max());

	std::vector<char> buffer;
	lt::entry e = t.generate();
	TEST_CHECK(!e["info"]["files tree"]["test"]["1-empty"].find_key("pieces root"));
	TEST_CHECK(!e["info"]["files tree"]["test"]["2-small"].find_key("pieces root"));
	TEST_CHECK(!e["info"]["files tree"]["test"]["3-small"].find_key("pieces root"));

	lt::bencode(std::back_inserter(buffer), e);
	lt::torrent_info info(buffer, lt::from_span);

	TEST_CHECK(info.files().root(0_file).is_all_zeros());
	TEST_CHECK(!info.files().root(1_file).is_all_zeros());
}

namespace {

std::string test_create_torrent(lt::file_storage& fs, int const piece_size
	, lt::create_flags_t const flags)
{
	lt::create_torrent ct(fs, piece_size, flags);
	ct.set_creation_date(1337);
	if (!(flags & lt::create_torrent::v2_only))
		for (lt::piece_index_t i : fs.piece_range())
			ct.set_hash(i, lt::sha1_hash::max());
	if (!(flags & lt::create_torrent::v1_only))
		for (auto const f : fs.file_range())
			if (!fs.pad_file_at(f))
				for (auto const p : fs.file_piece_range(f))
					ct.set_hash2(f, p, lt::sha256_hash::max());
	auto e = ct.generate();
	std::string buf;
	lt::bencode(std::back_inserter(buf), e);
	std::vector<char> buf2;
	lt::bencode(std::back_inserter(buf2), e);
	TEST_CHECK(buf2 == ct.generate_buf());
	return buf;
}

}

TORRENT_TEST(v1_tail_padding)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v1_only | lt::create_torrent::canonical_files)
		, "d13:creation datei1337e4:infod5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi16383e4:pathl7:2-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee");
}

// padding goes before empty files, not after
TORRENT_TEST(v1_empty_file_placement)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-empty", 0);
	fs.add_file("test/3-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v1_only | lt::create_torrent::canonical_files)
		, "d13:creation datei1337e4:infod5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi0e4:pathl7:2-emptyee"
		"d6:lengthi16383e4:pathl7:3-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee");
}

// despite the files being added in one order, the torrent is still created with
// files in the canonical order
TORRENT_TEST(v1_file_sorting)
{
	lt::file_storage fs;
	fs.add_file("test/2-small", 0x3fff);
	fs.add_file("test/1-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v1_only | lt::create_torrent::canonical_files)
		, "d13:creation datei1337e4:infod5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi16383e4:pathl7:2-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee");
}

// This is a backwards compatibility feature
TORRENT_TEST(v1_no_tail_padding)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v1_only | lt::create_torrent::canonical_files_no_tail_padding)
		, "d13:creation datei1337e4:infod5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi16383e4:pathl7:2-smallee"
		"e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee");
}

// padding goes after empty files in backwards compatibility mode
TORRENT_TEST(v1_empty_file_placement_backwards_compatibility)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-empty", 0);
	fs.add_file("test/3-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v1_only | lt::create_torrent::canonical_files_no_tail_padding)
		, "d13:creation datei1337e4:infod5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d6:lengthi0e4:pathl7:2-emptyee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi16383e4:pathl7:3-smallee"
		"e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee");
}

// the v1_only flag does not arrange files canonically (i.e. no ordering nor
// padding)
TORRENT_TEST(v1_no_padding)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v1_only)
		, "d13:creation datei1337e4:infod5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d6:lengthi16383e4:pathl7:2-smallee"
		"e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee");
}

TORRENT_TEST(hybrid)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, {})
		, "d13:creation datei1337e4:infod"
		"9:file tree"
		"d7:1-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"7:2-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"e"
		"5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi16383e4:pathl7:2-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"e"
		"12:meta versioni2e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"e"
		"12:piece layersde"
		"e");
}

TORRENT_TEST(hybrid_single_file)
{
	lt::file_storage fs;
	fs.add_file("1-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, {})
		, "d13:creation datei1337e4:infod"
		"9:file tree"
		"d7:1-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"e"
		"6:lengthi16383e"
		"12:meta versioni2e"
		"4:name7:1-small12:piece lengthi16384e"
		"6:pieces20:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"e"
		"12:piece layersde"
		"e");
}

TORRENT_TEST(hybrid_single_file_with_directory)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, {})
		, "d13:creation datei1337e4:infod"
		"9:file tree"
		"d7:1-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"e"
		"5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"e"
		"12:meta versioni2e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces20:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"e"
		"12:piece layersde"
		"e");
}

// this is a backwards compatibility feature
TORRENT_TEST(hybrid_no_tail_padding)
{
	lt::file_storage fs;
	fs.add_file("test/1-small", 0x3fff);
	fs.add_file("test/2-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::canonical_files_no_tail_padding)
		, "d13:creation datei1337e4:infod"
		"9:file tree"
		"d7:1-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"7:2-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"e"
		"5:filesl"
		"d6:lengthi16383e4:pathl7:1-smallee"
		"d4:attr1:p6:lengthi1e4:pathl4:.pad1:1ee"
		"d6:lengthi16383e4:pathl7:2-smallee"
		"e"
		"12:meta versioni2e"
		"4:name4:test12:piece lengthi16384e"
		"6:pieces40:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"e"
		"12:piece layersde"
		"e");
}

TORRENT_TEST(v2_only_file_sorting)
{
	lt::file_storage fs;
	fs.add_file("test/2-small", 0x3fff);
	fs.add_file("test/1-small", 0x3fff);
	TEST_EQUAL(test_create_torrent(fs, 0x4000, lt::create_torrent::v2_only)
		, "d13:creation datei1337e4:infod"
		"9:file tree"
		"d7:1-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"7:2-smalld0:d6:lengthi16383e11:pieces root32:"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"ee"
		"e"
		"12:meta versioni2e"
		"4:name4:test12:piece lengthi16384e"
		"e"
		"12:piece layersde"
		"e");
}
