/*

Copyright (c) 2020, Arvid Norberg
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

#include "libtorrent/session.hpp" // for default_disk_io_constructor
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/add_torrent_params.hpp"

// TODO: remove this dependency
#include "libtorrent/aux_/path.hpp"

#include <random>
#include <algorithm>
#include <vector>
#include <iostream>

using disk_test_mode_t = lt::flags::bitfield_flag<std::uint8_t, struct disk_test_mode_tag>;

using lt::operator""_bit;
using lt::operator "" _sv;

namespace test_mode {
constexpr disk_test_mode_t sparse = 0_bit;
constexpr disk_test_mode_t even_file_sizes = 1_bit;
constexpr disk_test_mode_t read_random_order = 2_bit;
constexpr disk_test_mode_t flush_files = 3_bit;
}

std::mt19937 random_engine(std::random_device{}());

// TODO: in C++17, use std::filesystem
void remove_all(std::string path)
{
#ifdef TORRENT_WINDOWS
	WIN32_FIND_DATA data;
	HANDLE list = ::FindFirstFile(path.c_str(), &data);
	if (list == INVALID_HANDLE_VALUE)
	{
		::DeleteFile(path.c_str());
		return;
	}

	do
	{
		if (data.cFileName != "."_sv && data.cFileName != ".."_sv)
		{
			remove_all(path + "\\" + data.cFileName);
		}
	} while(FindNextFile(list, &data));
	FindClose(list);
	RemoveDirectory(path.c_str());
#else
	DIR* handle = ::opendir(path.c_str());
	if (handle == nullptr)
	{
		::remove(path.c_str());
		return;
	}

	dirent* de = ::readdir(handle);
	while (de != nullptr)
	{
		if (de->d_name != "."_sv && de->d_name != ".."_sv)
		{
			remove_all(path + "/" + de->d_name);
		}
		de = ::readdir(handle);
	}
	::closedir(handle);
	::remove(path.c_str());
#endif
}

int run_test(disk_test_mode_t const flags
	, int const num_threads
	, int const file_pool_size
	, int const num_files
	, int const queue_limit
	, int const read_multiplier) try
{
	lt::io_context ioc;
	lt::counters cnt;
	lt::settings_pack pack;
	pack.set_int(lt::settings_pack::aio_threads, num_threads);
	pack.set_int(lt::settings_pack::file_pool_size, file_pool_size);

	std::unique_ptr<lt::disk_interface> disk_io
		= lt::default_disk_io_constructor(ioc, pack, cnt);

	lt::file_storage fs;

	std::int64_t file_size = (flags & test_mode::even_file_sizes)
		? 0x1000
		: 1337;
	for (int i = 0; i < num_files; ++i)
	{
		fs.add_file("test/" + std::to_string(i), file_size);
		file_size *= 2;
	}

	std::int64_t const total_size = fs.total_size();
	int const piece_size = 0x8000;
	int const blocks_per_piece = std::max(1, piece_size / lt::default_block_size);
	int const num_pieces = static_cast<int>((total_size + piece_size - 1) / piece_size);
	fs.set_num_pieces(num_pieces);
	fs.set_piece_length(piece_size);

	std::cerr << "RUNNING: "
		<< ((flags & test_mode::sparse) ? "s-" : "f-")
		<< ((flags & test_mode::even_file_sizes) ? "e-" : "o-")
		<< ((flags & test_mode::read_random_order) ? "rr-" : "or-")
		<< ((flags & test_mode::flush_files) ? "f-" : "a-")
		<< num_pieces << '-'
		<< file_pool_size << '-'
		<< queue_limit << '-'
		<< read_multiplier
		<< ": ";

	// TODO: in C++17, use std::filesystem
	remove_all("scratch-area");

	// TODO: add test mode where some file priorities are 0

	lt::aux::vector<lt::download_priority_t, lt::file_index_t> prios;
	std::string save_path = "./scratch-area";
	lt::storage_params params(fs, nullptr
		, save_path
		, (flags & test_mode::sparse) ? lt::storage_mode_sparse : lt::storage_mode_allocate
		, prios
		, lt::sha1_hash("01234567890123456789"));

	lt::storage_holder t = disk_io->new_torrent(params, {});

	std::vector<lt::peer_request> blocks_to_write;
	for (int p = 0; p < num_pieces; ++p)
	{
		for (int b = 0; b < blocks_per_piece; ++b)
		{
			blocks_to_write.push_back(
				{lt::piece_index_t{p}, b * lt::default_block_size, lt::default_block_size});
		}
	}
	std::shuffle(blocks_to_write.begin(), blocks_to_write.end(), random_engine);

	std::vector<lt::peer_request> blocks_to_read;
	blocks_to_read.reserve(blocks_to_write.size());

	std::vector<char> write_buffer(lt::default_block_size);

	int outstanding = 0;

	lt::add_torrent_params atp;

	disk_io->async_check_files(t, &atp, lt::aux::vector<std::string, lt::file_index_t>{}
		, [&](lt::status_t, lt::storage_error const&) { --outstanding; });
	++outstanding;
	disk_io->submit_jobs();

	while (outstanding > 0)
	{
		ioc.run_one();
		ioc.restart();
	}

	int job_counter = 0;

	while (!blocks_to_write.empty()
		|| !blocks_to_read.empty()
		|| outstanding > 0)
	{
		for (int i = 0; i < read_multiplier; ++i)
		{
			if (!blocks_to_read.empty() && outstanding < queue_limit)
			{
				auto const req = blocks_to_read.back();
				blocks_to_read.erase(blocks_to_read.end() - 1);

				disk_io->async_read(t, req
					, [&](lt::disk_buffer_holder h, lt::storage_error const& ec)
					{
						TORRENT_UNUSED(h);
						--outstanding;
						++job_counter;
						if (ec) throw std::runtime_error("async_read failed " + ec.ec.message());
						// TODO: validate that we read the correct data. buffer
						// in h
					});

				++outstanding;
			}
		}

		if (!blocks_to_write.empty() && outstanding < queue_limit)
		{
			auto const req = blocks_to_write.back();
			blocks_to_write.erase(blocks_to_write.end() - 1);

			// TODO: put a pattern in write_buffer that can be validated in read
			// operations
			disk_io->async_write(t, req, write_buffer.data()
				, {}, [&,req](lt::storage_error const& ec)
				{
					--outstanding;
					++job_counter;
					if (ec) throw std::runtime_error("async_write failed " + ec.ec.message());
					if (flags & test_mode::read_random_order)
					{
						std::uniform_int_distribution<> d(0, int(blocks_to_read.size()));
						blocks_to_read.insert(blocks_to_read.begin() + d(random_engine), req);
					}
					else
					{
						blocks_to_read.push_back(req);
					}
					// if read_multiplier > 1, put this block more times in the
					// read queue
					for (int i = 1; i < read_multiplier; ++i)
					{
						std::uniform_int_distribution<> d(0, int(blocks_to_read.size()));
						blocks_to_read.insert(blocks_to_read.begin() + d(random_engine), req);
					}
				});

			++outstanding;
		}

		if ((flags & test_mode::flush_files) && (job_counter % 500) == 499)
		{
			disk_io->async_release_files(t, [&]()
				{
					--outstanding;
					++job_counter;
				});
			++outstanding;
		}

		// TODO: add test_mode for async_move_storage
		// TODO: add test_mode for async_hash and async_hash2
		// TODO: add test_mode for abort_hash_jobs
		// TODO: add test_mode for async_delete_files
		// TODO: add test_mode for async_rename_file
		// TODO: add test_mode for async_set_file_priority

		disk_io->submit_jobs();
		if (outstanding >= queue_limit)
			ioc.run_one();
		else
			ioc.poll();
		ioc.restart();
	}

	disk_io->remove_torrent(t);

	disk_io->abort(true);

	std::cerr << "OK\n";
	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "FAILED WITH EXCEPTION: " << e.what() << '\n';
	return 1;
}

int main(int, char const*[])
{
	// TODO: make it possible to run a test with all custom arguments from the
	// command line

	int num_files = 20;
	int queue_size = 32;
	int num_threads = 16;
	int read_multiplier = 3;
	int file_pool_size = 10;

	int ret = 0;
	ret |= run_test(test_mode::sparse, num_threads, file_pool_size, num_files, queue_size, read_multiplier);
	ret |= run_test(test_mode::sparse | test_mode::even_file_sizes, num_threads, file_pool_size, num_files, queue_size, read_multiplier);
	ret |= run_test(test_mode::read_random_order | test_mode::sparse, num_threads, file_pool_size, num_files, queue_size, read_multiplier);
	ret |= run_test(test_mode::read_random_order | test_mode::sparse | test_mode::even_file_sizes, num_threads, file_pool_size, num_files, queue_size, read_multiplier);
	ret |= run_test(test_mode::flush_files | test_mode::read_random_order | test_mode::sparse | test_mode::even_file_sizes, num_threads, file_pool_size, num_files, queue_size, read_multiplier);

	return ret;
}

