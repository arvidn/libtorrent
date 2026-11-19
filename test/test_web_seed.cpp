/*

Copyright (c) 2007-2008, 2010, 2013-2015, 2017, 2020-2022, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "disk_io_test.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"
#include "libtorrent/aux_/web_peer_connection.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/path.hpp"
#include "test_utils.hpp"

#include <array>
#include <cstdio>

using namespace lt;

namespace {

int const url_size = 56;
int const piece_size = 0x4000;

// these mirror the constants in web_peer_connection.cpp
int const get_request_overhead = 200;
int const min_payload_request_ratio = 4;

// every file in these tests is given a path of this length, so the request
// size per file is uniform
int const path_size = 9;
int const get_size = url_size + get_request_overhead + path_size;

// the number of files a piece may span before being cleared
int const max_files = piece_size / (get_size * min_payload_request_ratio);

std::string file_path(file_storage const& fs)
{
	std::array<char, 5> name{};
	std::snprintf(name.data(), name.size(), "%04d", fs.num_files());
	std::string const ret = combine_path("test", name.data());
	TEST_EQUAL(int(ret.size()), path_size);
	return ret;
}

void add_file(file_storage& fs, std::int64_t const size, file_flags_t const flags = {})
{
	fs.add_file_borrow({}, file_path(fs), size, flags);
}

void add_small_files(file_storage& fs, int const count)
{
	for (int i = 0; i < count; ++i)
		add_file(fs, 1);
}

typed_bitfield<piece_index_t> expensive_pieces(file_storage& fs, int const url = url_size)
{
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	typed_bitfield<piece_index_t> have;
	have.resize(fs.num_pieces(), true);
	aux::clear_expensive_pieces(fs, url, have);
	return have;
}

}

TORRENT_TEST(expensive_pieces_single_file)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	add_file(fs, piece_size * 3 + 1);
	TEST_CHECK(expensive_pieces(fs).all_set());
}

TORRENT_TEST(expensive_pieces_at_limit)
{
	TEST_CHECK(max_files > 2);
	file_storage fs;
	fs.set_piece_length(piece_size);
	// piece 0 holds exactly max_files files, piece 1 one more. Both are
	// padded out to full pieces by a larger file
	add_small_files(fs, max_files - 1);
	add_file(fs, piece_size - (max_files - 1));
	add_small_files(fs, max_files);
	add_file(fs, piece_size - max_files);
	add_file(fs, piece_size);

	auto const have = expensive_pieces(fs);
	TEST_EQUAL(have.size(), 3);
	TEST_CHECK(have.get_bit(0_piece));
	TEST_CHECK(!have.get_bit(1_piece));
	TEST_CHECK(have.get_bit(2_piece));
}

TORRENT_TEST(expensive_pieces_file_spanning_pieces)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	// the large file starts in piece 0 and ends in piece 3. It counts
	// towards the small files in piece 0 and in piece 3, but the pieces
	// in between only hold the one file
	add_small_files(fs, max_files);
	add_file(fs, piece_size * 3);
	add_small_files(fs, max_files);

	auto const have = expensive_pieces(fs);
	TEST_EQUAL(have.size(), 4);
	TEST_CHECK(!have.get_bit(0_piece));
	TEST_CHECK(have.get_bit(1_piece));
	TEST_CHECK(have.get_bit(2_piece));
	TEST_CHECK(!have.get_bit(3_piece));
}

TORRENT_TEST(expensive_pieces_short_last_piece)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	add_file(fs, piece_size);
	// a short last piece is judged by the full piece length
	add_small_files(fs, max_files);

	auto const have = expensive_pieces(fs);
	TEST_EQUAL(have.size(), 2);
	TEST_CHECK(have.all_set());
}

TORRENT_TEST(expensive_pieces_pad_files)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	// pad files are not requested and don't count
	for (int i = 0; i < max_files * 2; ++i)
		add_file(fs, 1, file_storage::flag_pad_file);
	add_file(fs, piece_size);

	auto const have = expensive_pieces(fs);
	TEST_EQUAL(have.size(), 2);
	TEST_CHECK(have.all_set());
}

TORRENT_TEST(expensive_pieces_long_paths)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	// two files whose paths together exceed the request budget for a piece,
	// while one of them fits. A single filename is limited in length, a
	// directory name is not
	std::string const long_dir =
		combine_path("test", std::string(piece_size / min_payload_request_ratio / 2, 'x'));
	fs.add_file_borrow({}, combine_path(long_dir, "0"), piece_size / 2);
	fs.add_file_borrow({}, combine_path(long_dir, "1"), piece_size / 2);
	fs.add_file_borrow({}, combine_path(long_dir, "2"), piece_size);

	auto const have = expensive_pieces(fs);
	TEST_EQUAL(have.size(), 2);
	TEST_CHECK(!have.get_bit(0_piece));
	TEST_CHECK(have.get_bit(1_piece));
}

TORRENT_TEST(expensive_pieces_huge_url)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	add_file(fs, piece_size * 2);
	add_file(fs, piece_size * 2);

	// a URL this large can never be amortized
	TEST_CHECK(expensive_pieces(fs, piece_size).none_set());
}

const int proxy = lt::settings_pack::none;

#if TORRENT_USE_SSL
TORRENT_TEST_DISK_IO(web_seed_ssl) { run_http_suite(proxy, "https", disk_io); }
#endif

TORRENT_TEST_DISK_IO(web_seed) { run_http_suite(proxy, "http", disk_io); }
