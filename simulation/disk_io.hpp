/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef SIMULATION_DISK_IO_HPP
#define SIMULATION_DISK_IO_HPP

#include "libtorrent/disk_interface.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/io_context.hpp"
#include "test_utils.hpp"

#include <array>
#include <limits>
#include <memory>
#include <string>

// content_period, when non-zero, makes the generated content repeat every
// this many pieces
std::array<char, 0x4000> generate_block_fill(lt::piece_index_t p, int block, int content_period);
lt::sha1_hash generate_hash1(
	lt::piece_index_t p, int piece_size, int pad_bytes, int content_period);
lt::sha1_hash generate_hash2(lt::piece_index_t p,
	int piece_size,
	int piece_size2,
	lt::span<lt::sha256_hash> hashes,
	int pad_bytes,
	int content_period);
lt::sha256_hash generate_block_hash(lt::piece_index_t p, int offset, int content_period);
void generate_block(char* b, lt::peer_request r, int pad_bytes, int piece_size, int content_period);
lt::add_torrent_params create_test_torrent(int piece_size,
	int num_pieces,
	lt::create_flags_t flags,
	int num_files = 1,
	bool bad_v1_hashes = false,
	std::string const& ssl_root_cert = std::string());
lt::add_torrent_params create_test_torrent(
	int num_pieces, lt::create_flags_t flags, int blocks_per_piece, int num_files = 1
	, bool bad_v1_hashes = false);

// a v2-only torrent with two identical files of num_pieces pieces each. The
// test_disk must be configured with set_content_period(num_pieces)
lt::add_torrent_params create_identical_files_torrent(int num_pieces, int blocks_per_piece);

enum class existing_files_mode : std::uint8_t
{
	no_files, full_invalid, partial_valid, full_valid
};

struct test_disk
{
	test_disk set_seed(bool const s = true) const
	{
		return set_files(s ? existing_files_mode::full_valid : existing_files_mode::no_files);
	}
	test_disk set_files(existing_files_mode const s) const
	{
		auto ret = *this;
		ret.files = s;
		return ret;
	}
	test_disk set_space_left(int const left) const
	{
		auto ret = *this;
		ret.space_left = left;
		return ret;
	}
	test_disk set_recover_full_disk() const
	{
		auto ret = *this;
		ret.recover_full_disk = true;
		return ret;
	}
	test_disk send_corrupt_data(
		int const blocks, lt::piece_index_t const piece = lt::piece_index_t{-1}) const
	{
		auto ret = *this;
		ret.corrupt_data_in = blocks;
		ret.corrupt_piece = piece;
		return ret;
	}
	test_disk set_content_period(int const pieces) const
	{
		auto ret = *this;
		ret.content_period = pieces;
		return ret;
	}

	// the number of blocks/write jobs in the queue before we exceed the write
	// queue size. Once the level drops below the low watermark, we allow writes
	// again
	int high_watermark = 50;
	int low_watermark = 40;

	std::unique_ptr<lt::disk_interface> operator()(
		lt::io_context& ioc, lt::settings_interface const&, lt::counters&);

	// seek time in front of every read and write
	lt::time_duration seek_time = lt::milliseconds(10);

	// hash time per block
	lt::time_duration hash_time = lt::microseconds(15);

	// write time per block
	lt::time_duration write_time = lt::microseconds(2);

	// read time per block
	lt::time_duration read_time = lt::microseconds(1);

	// when checking files, say we have some files on disk already (but not with
	// valid data)
	existing_files_mode files = existing_files_mode::no_files;

	// after having failed with disk-full error, reset space_left to int_max
	bool recover_full_disk = false;

	// after sending this many blocks, send corrupt data
	int corrupt_data_in = std::numeric_limits<int>::max();

	// restricts corrupt_data_in to blocks of this piece; the default
	// (-1) matches every piece
	lt::piece_index_t corrupt_piece{-1};

	// after having written this many bytes, fail with disk-full
	int space_left = std::numeric_limits<int>::max();

	// number of read buffers handed to the engine non-exclusively. Asserted
	// non-zero, so a test cannot pass without having exercised that path. A
	// shared_ptr for the same reason as num_hash_calls
	std::shared_ptr<int> non_exclusive_reads = std::make_shared<int>(0);

	// number of async_hash()/async_hash2() calls dispatched so far. A
	// shared_ptr so it keeps counting across the copies test_disk's fluent
	// setters make, and so the caller can read it back after handing a
	// copy to disk_io_constructor.
	std::shared_ptr<int> num_hash_calls = std::make_shared<int>(0);

	// when non-zero, the generated content repeats every this many pieces, which
	// makes piece-aligned files of this many pieces identical
	int content_period = 0;
};

std::ostream& operator<<(std::ostream& os, existing_files_mode const mode);

#endif
