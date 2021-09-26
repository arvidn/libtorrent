/*

Copyright (c) 2021, Arvid Norberg
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

#include "libtorrent/disk_interface.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/io_context.hpp"
#include "test_utils.hpp"

#include <array>
#include <limits>

std::array<char, 0x4000> generate_block_fill(lt::piece_index_t const p, int const block);
lt::sha1_hash generate_hash1(lt::piece_index_t const p, lt::file_storage const& fs);
lt::sha1_hash generate_hash2(lt::piece_index_t p, lt::file_storage const& fs
	, lt::span<lt::sha256_hash> const hashes);
lt::sha256_hash generate_block_hash(lt::piece_index_t p, int offset);
void generate_block(char* b, lt::peer_request const& r);
std::shared_ptr<lt::torrent_info> create_test_torrent(int piece_size
	, int num_pieces, lt::create_flags_t flags, int num_files = 1);
lt::add_torrent_params create_test_torrent(
	int num_pieces, lt::create_flags_t flags, int blocks_per_piece, int num_files = 1);

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
	test_disk send_corrupt_data(int const blocks) const
	{
		auto ret = *this;
		ret.corrupt_data_in = blocks;
		return ret;
	}

	// the number of blocks/write jobs in the queue before we exceed the write
	// queue size. Once the level drops below the low watermark, we allow writes
	// again
	int high_watermark = 50;
	int low_watermark = 40;

	std::unique_ptr<lt::disk_interface> operator()(
		lt::io_context& ioc, lt::settings_interface const&, lt::counters&);

	// seek time in fron of every read and write
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

	// after having written this many bytes, fail with disk-full
	int space_left = std::numeric_limits<int>::max();
};

