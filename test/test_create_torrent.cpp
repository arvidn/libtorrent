/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2016-2020, Arvid Norberg
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2017-2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/units.hpp"

#include <cstring>

constexpr lt::file_index_t operator""_file (unsigned long long int const v)
{ return lt::file_index_t{static_cast<int>(v)}; }


// make sure creating a torrent from an existing handle preserves the
// info-dictionary verbatim, so as to not alter the info-hash
TORRENT_TEST(create_verbatim_torrent)
{
	char const test_torrent[] = "d4:infod4:name6:foobar6:lengthi12345e"
		"12:piece lengthi65536e6:pieces20:ababababababababababee";

	lt::torrent_info info(test_torrent, lt::from_span);

	info.add_tracker("http://test.com");
	info.add_tracker("http://test.com");
	TEST_EQUAL(info.trackers().size(), 1);

	lt::create_torrent t(info);

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());

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
	lt::torrent_info info2(buffer, lt::from_span);

	TEST_EQUAL(info2.comment(), "this is a test comment");
	TEST_EQUAL(info2.trackers().size(), 1);
	TEST_EQUAL(info2.trackers().front().url, "udp://testurl.com/announce");
	TEST_CHECK(info1.info_hashes() == info2.info_hashes());
	TEST_CHECK(info2.hash_for_piece(lt::piece_index_t(0)) == info1.hash_for_piece(lt::piece_index_t(0)));
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

TORRENT_TEST(create_torrent_round_trip_hybrid)
{
	test_round_trip_torrent("v2_hybrid.torrent");
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
		t.set_hash2(lt::file_index_t(0), zero, dummy);
		t.set_hash2(lt::file_index_t(1), zero, dummy);
		t.set_hash2(lt::file_index_t(2), zero, dummy);
		t.set_hash2(lt::file_index_t(3), zero, dummy);
		TEST_THROW(t.generate());
	}
}

TORRENT_TEST(v2_only)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	fs.add_file("test/B", 0x4002);
	lt::create_torrent t(fs, 0x4000, lt::create_torrent::v2_only);

	using p = lt::piece_index_t::diff_type;
	t.set_hash2(lt::file_index_t(0), p(0), lt::sha256_hash::max());
	t.set_hash2(lt::file_index_t(0), p(1), lt::sha256_hash::max());
	t.set_hash2(lt::file_index_t(0), p(2), lt::sha256_hash::max());
	// file 1 is a pad file
	t.set_hash2(lt::file_index_t(2), p(0), lt::sha256_hash::max());
	t.set_hash2(lt::file_index_t(2), p(1), lt::sha256_hash::max());

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
	lt::torrent_info info(buffer, lt::from_span);
	TEST_CHECK(info.info_hashes().has_v2());
	TEST_CHECK(!info.info_hashes().has_v1());
	TEST_EQUAL(info.files().file_name(0_file), "A");
	TEST_EQUAL(info.files().pad_file_at(1_file), true);
	TEST_EQUAL(info.files().file_name(2_file), "B");
	TEST_EQUAL(info.name(), "test");

	lt::create_torrent t2(info);
	std::vector<char> buffer2;
	lt::bencode(std::back_inserter(buffer2), t.generate());

	TEST_CHECK(buffer == buffer2);
}

TORRENT_TEST(v2_only_set_hash)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	lt::create_torrent t(fs, 0x4000, lt::create_torrent::v2_only);

	TEST_THROW(t.set_hash(lt::piece_index_t(0), lt::sha1_hash::max()));
}

TORRENT_TEST(v1_only_set_hash2)
{
	lt::file_storage fs;
	fs.add_file("test/A", 0x8002);
	lt::create_torrent t(fs, 0x4000, lt::create_torrent::v1_only);

	using p = lt::piece_index_t::diff_type;
	TEST_THROW(t.set_hash2(lt::file_index_t(0), p(0), lt::sha256_hash::max()));
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
	t.set_hash2(lt::file_index_t(0), p(0), lt::sha256_hash::max());
	t.set_hash2(lt::file_index_t(0), p(1), lt::sha256_hash::max());
	t.set_hash2(lt::file_index_t(0), p(2), lt::sha256_hash::max());
	// file 1 is a pad file
	t.set_hash2(lt::file_index_t(2), p(0), lt::sha256_hash::max());
	t.set_hash2(lt::file_index_t(2), p(1), lt::sha256_hash::max());

	std::vector<char> buffer;
	lt::bencode(std::back_inserter(buffer), t.generate());
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
	lt::torrent_info info(buffer, lt::from_span);
	TEST_CHECK(!info.info_hashes().has_v2());
	TEST_CHECK(info.info_hashes().has_v1());
	TEST_EQUAL(info.files().file_name(0_file), "A");
	TEST_EQUAL(info.files().pad_file_at(1_file), true);
	TEST_EQUAL(info.files().file_name(2_file), "B");
	TEST_EQUAL(info.name(), "test");
}
