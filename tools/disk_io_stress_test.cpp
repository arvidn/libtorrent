/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp" // for default_disk_io_constructor
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"

#include "libtorrent/disk_interface.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/scope_end.hpp"

// TODO: remove this dependency
#include "libtorrent/aux_/path.hpp"

#include <random>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iomanip>

using disk_test_mode_t = lt::flags::bitfield_flag<std::uint8_t, struct disk_test_mode_tag>;

using lt::operator""_bit;
using lt::operator "" _sv;

namespace test_mode {
constexpr disk_test_mode_t sparse = 0_bit;
constexpr disk_test_mode_t even_file_sizes = 1_bit;
constexpr disk_test_mode_t read_random_order = 2_bit;
constexpr disk_test_mode_t flush_files = 3_bit;
constexpr disk_test_mode_t clear_pieces = 4_bit;
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

bool check_block_fill(lt::peer_request const& req, lt::span<char const> buf)
{
	int const v = (static_cast<int>(req.piece) << 8) | ((req.start / lt::default_block_size) & 0xff);
	int offset = 0;
	int const tail = buf.size() % 4;
	for (; offset < buf.size() - tail; offset += 4)
		if (std::memcmp(buf.data() + offset, reinterpret_cast<char const*>(&v), 4) != 0)
		{
			std::cout << "buffer diverged at word: " << offset << '\n';
			return false;
		}
	if (tail > 0)
		if (std::memcmp(buf.data() + offset, reinterpret_cast<char const*>(&v), tail) != 0)
		{
			std::cout << "buffer diverged at word: " << offset << '\n';
			return false;
		}
	return true;
}

void generate_block_fill(lt::peer_request const& req, lt::span<char> buf)
{
	int const v = (static_cast<int>(req.piece) << 8) | ((req.start / lt::default_block_size) & 0xff);
	int offset = 0;
	int const tail = buf.size() % 4;
	for (; offset < buf.size() - tail; offset += 4)
		std::memcpy(buf.data() + offset, reinterpret_cast<char const*>(&v), 4);
	if (tail > 0)
		std::memcpy(buf.data() + offset, reinterpret_cast<char const*>(&v), tail);
}

struct test_case
{
	int num_files;
	int queue_size;
	int num_threads;
	int read_multiplier;
	int file_pool_size;
	disk_test_mode_t flags;
	std::string disk_backend;
};

int run_test(test_case const& t)
{
	lt::file_storage fs;

	std::int64_t file_size = (t.flags & test_mode::even_file_sizes)
		? 0x1000
		: 1337;

	int const piece_size = 0x8000;

	{
		for (int i = 0; i < t.num_files; ++i)
		{
			fs.add_file("test/" + std::to_string(i), file_size);
			file_size *= 2;
		}
		std::int64_t const total_size = fs.total_size();
		int const num_pieces = static_cast<int>((total_size + piece_size - 1) / piece_size);
		fs.set_num_pieces(num_pieces);
		fs.set_piece_length(piece_size);
	}
	lt::io_context ioc;
	lt::counters cnt;
	lt::settings_pack pack;
	pack.set_int(lt::settings_pack::aio_threads, t.num_threads);
	pack.set_int(lt::settings_pack::file_pool_size, t.file_pool_size);
	pack.set_int(lt::settings_pack::max_queued_disk_bytes, t.queue_size * lt::default_block_size);

	std::unique_ptr<lt::disk_interface> disk_io;

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
	if (t.disk_backend == "mmap"_sv)
		disk_io = lt::mmap_disk_io_constructor(ioc, pack, cnt);
	else
#endif
	{
		if (t.disk_backend  == "posix"_sv)
			disk_io = lt::posix_disk_io_constructor(ioc, pack, cnt);
		else if (t.disk_backend  == "disabled"_sv)
			disk_io = lt::disabled_disk_io_constructor(ioc, pack, cnt);
		else
		{
			if (t.disk_backend != "default")
			{
				std::fprintf(stderr, "unknown disk-io subsystem: \"%s\". Using default.\n", t.disk_backend.c_str());
			}
			disk_io = lt::default_disk_io_constructor(ioc, pack, cnt);
		}
	}

	std::cerr << "RUNNING: -f " << t.num_files
		<< " -q " << t.queue_size
		<< " -t " << t.num_threads
		<< " -r " << t.read_multiplier
		<< " -p " << t.file_pool_size
		<< ((t.flags & test_mode::sparse) ? "" : " alloc")
		<< ((t.flags & test_mode::even_file_sizes) ? " even-size" : "")
		<< ((t.flags & test_mode::read_random_order) ? " random-read" : "")
		<< ((t.flags & test_mode::flush_files) ? " flush" : "")
		<< ((t.flags & test_mode::clear_pieces) ? " clear" : "")
		<< " -d " << t.disk_backend
		<< "\n";

	try
	{
		// TODO: in C++17, use std::filesystem
		remove_all("scratch-area");

		// TODO: add test mode where some file priorities are 0

		lt::aux::vector<lt::download_priority_t, lt::file_index_t> prios;
		std::string save_path = "./scratch-area";
		lt::renamed_files rf;
		lt::storage_params params(fs, rf
			, save_path
			, (t.flags & test_mode::sparse) ? lt::storage_mode_sparse : lt::storage_mode_allocate
			, prios
			, lt::sha1_hash("01234567890123456789"), true, true);

		auto abort_disk = lt::aux::scope_end([&] { disk_io->abort(true); });

		lt::storage_holder const tor = disk_io->new_torrent(params, {});

		std::vector<lt::peer_request> blocks_to_write;
		for (lt::piece_index_t p : fs.piece_range())
		{
			int const local_piece_size = fs.piece_size(p);
			for (int offset = 0, left = local_piece_size;
				offset < local_piece_size;
				offset += lt::default_block_size, left -= lt::default_block_size)
			{
				blocks_to_write.push_back(
					{lt::piece_index_t{p}, offset, std::min(lt::default_block_size, left)});
			}
		}
		std::shuffle(blocks_to_write.begin(), blocks_to_write.end(), random_engine);

		lt::aux::vector<lt::peer_request> blocks_to_read;
		blocks_to_read.reserve(blocks_to_write.size());

		std::vector<char> write_buffer(lt::default_block_size);

		int outstanding = 0;
		std::set<int> in_flight;

		lt::add_torrent_params atp;

		int job_idx = 0;
		in_flight.insert(job_idx);
		++outstanding;
		disk_io->async_check_files(tor, &atp, lt::aux::vector<std::string, lt::file_index_t>{}
			, [&, job_idx](lt::status_t, lt::storage_error const&) {
				TORRENT_ASSERT(in_flight.count(job_idx));
				in_flight.erase(job_idx);
				TORRENT_ASSERT(outstanding > 0);
				--outstanding;
			});
		++job_idx;
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
			if ((job_counter & 0x1fff) == 0)
			{
				printf("o: %d w: %d r: %d\r"
					, outstanding
					, int(blocks_to_write.size())
					, int(blocks_to_read.size()));
				fflush(stdout);
			}
			for (int i = 0; i < t.read_multiplier; ++i)
			{
				if (!blocks_to_read.empty() && outstanding < t.queue_size)
				{
					auto const req = blocks_to_read.back();
					blocks_to_read.erase(blocks_to_read.end() - 1);

					in_flight.insert(job_idx);
					++outstanding;
					disk_io->async_read(tor, req, [&, req, job_idx](lt::disk_buffer_holder h, lt::storage_error const& ec)
					{
						TORRENT_ASSERT(in_flight.count(job_idx));
						in_flight.erase(job_idx);
						TORRENT_ASSERT(outstanding > 0);
						--outstanding;
						++job_counter;
						if (ec)
						{
							std::cerr << "async_write() failed: " << ec.ec.message()
								<< " " << lt::operation_name(ec.operation)
								<< " " << static_cast<int>(ec.file()) << "\n";
							throw std::runtime_error("async_read failed");
						}

						int const block_size = std::min((fs.piece_size(req.piece) - req.start), int(h.size()));
						if (!check_block_fill(req, {h.data(), block_size}))
						{
							std::cerr << "read buffer mismatch: (" << req.piece << ", " << req.start << ")\n";
							throw std::runtime_error("read buffer mismatch!");
						}
					});
					++job_idx;
				}
			}

			if (!blocks_to_write.empty() && outstanding < t.queue_size)
			{
				auto const req = blocks_to_write.back();
				blocks_to_write.erase(blocks_to_write.end() - 1);

				generate_block_fill(req, {write_buffer.data(), lt::default_block_size});

				in_flight.insert(job_idx);
				++outstanding;
				disk_io->async_write(tor, req, write_buffer.data()
					, {}, [&, job_idx](lt::storage_error const& ec)
					{
						TORRENT_ASSERT(in_flight.count(job_idx));
						in_flight.erase(job_idx);
						TORRENT_ASSERT(outstanding > 0);
						--outstanding;
						++job_counter;
						if (ec)
						{
							std::cerr << "async_write() failed: " << ec.ec.message()
								<< " " << lt::operation_name(ec.operation)
								<< " " << static_cast<int>(ec.file()) << "\n";
							throw std::runtime_error("async_write failed");
						}
					});
				++job_idx;
				if (t.flags & test_mode::read_random_order)
				{
					std::uniform_int_distribution<> d(0, blocks_to_read.end_index());
					blocks_to_read.insert(blocks_to_read.begin() + d(random_engine), req);
				}
				else
				{
					blocks_to_read.push_back(req);
				}
				// if read_multiplier > 1, put this block more times in the
				// read queue
				for (int i = 1; i < t.read_multiplier; ++i)
				{
					std::uniform_int_distribution<> d(0, blocks_to_read.end_index());
					blocks_to_read.insert(blocks_to_read.begin() + d(random_engine), req);
				}
			}

			if ((t.flags & test_mode::flush_files) && (job_counter % 500) == 499)
			{
				in_flight.insert(job_idx);
				++outstanding;
				disk_io->async_release_files(tor, [&, job_idx]()
				{
					TORRENT_ASSERT(in_flight.count(job_idx));
					in_flight.erase(job_idx);
					TORRENT_ASSERT(outstanding > 0);
					--outstanding;
					++job_counter;
				});
				++job_idx;
			}

			if ((t.flags & test_mode::clear_pieces) && (job_counter % 300) == 299)
			{
				lt::piece_index_t const p = blocks_to_write.front().piece;
				in_flight.insert(job_idx);
				++outstanding;
				disk_io->async_clear_piece(tor, p, [&, job_idx](lt::piece_index_t)
					{
					TORRENT_ASSERT(in_flight.count(job_idx));
					in_flight.erase(job_idx);
					TORRENT_ASSERT(outstanding > 0);
					--outstanding;
					++job_counter;
					});
				++job_idx;
				// TODO: technically all blocks for this piece should be added
				// to blocks_to_write again here
			}

			// TODO: add test_mode for async_move_storage
			// TODO: add test_mode for async_hash and async_hash2
			// TODO: add test_mode for abort_hash_jobs
			// TODO: add test_mode for async_delete_files
			// TODO: add test_mode for async_rename_file
			// TODO: add test_mode for async_set_file_priority

			disk_io->submit_jobs();
			if (outstanding >= t.queue_size)
				ioc.run_one();
			else
				ioc.poll();
			ioc.restart();
		}

		std::cerr << "OK (" << job_counter << " jobs)\n";
		return 0;
	}
	catch (std::exception const& e)
	{
		std::cerr << "FAILED WITH EXCEPTION: " << e.what() << '\n';

		auto const ps = fs.piece_length();
		for (lt::file_index_t f : fs.file_range())
		{
			auto const off = fs.file_offset(f);
			std::cout << " test/" << std::setw(2) << int(f)
			   << " size: " << std::setw(10) << fs.file_size(f)
			   << " first piece: (" << (off / ps) << " offset: " << (off % ps) << ")"
			   << '\n';
		}
		auto const total_size = fs.total_size();
		auto const num_pieces = fs.num_pieces();
		std::cout << "                           last piece: ("
		   << (total_size / ps) << " offset: " << (total_size % ps) << ")\n";
		std::cout << "num pieces: " << num_pieces << '\n';
		return 1;
	}
}

void print_usage()
{
	std::cerr << "USAGE: disk_io_stress_test <options>\n"
		"If no options are specified, the default suite of tests are run\n\n"
		"OPTIONS:\n"
		"   alloc\n"
		"      open files in pre-allocate mode\n"
		"   even-size\n"
		"      make test files even multiples of 1 kB\n"
		"   random-read\n"
		"      instead of reading blocks back in the same order they were written,\n"
		"      read them back in random order\n"
		"   flush\n"
		"      issue a 'release-files' disk job every 500 jobs\n"
		"   clear\n"
		"      issue a 'clear_piece' disk job every 300 jobs\n"
		"   -f <val>\n"
		"      specifies the number of files to use in the test torrent\n"
		"   -q <val>\n"
		"      specifies the job queue size. i.e. the max number of outstanding\n"
		"      jobs to post to the disk I/O subsystem\n"
		"   -t <val>\n"
		"      specifies the number of disk I/O threads to use\n"
		"   -r <val>\n"
		"      specifies the read multiplier. Each block that's written, is read this many times\n"
		"   -p <val>\n"
		"      specifies the file pool size. This is the number of files to keep open\n"
		;

}

int main(int argc, char const* argv[])
{
	if (argc == 1)
	{
		// the default test suite
		namespace tm = test_mode;

		test_case tests[] = {
			// files, queue, threads, read-mult, pool, flags, disk_backend
			{20, 32, 16, 3, 10, tm::sparse | tm::even_file_sizes, "default"},
			{20, 32, 16, 3, 10, tm::sparse, "default"},
			{20, 32, 16, 3, 10, tm::sparse | tm::read_random_order, "default"},
			{20, 32, 16, 3, 10, tm::sparse | tm::read_random_order | tm::even_file_sizes, "default"},
			{20, 32, 16, 3, 10, tm::flush_files | tm::sparse | tm::read_random_order | tm::even_file_sizes, "default"},

			// test with small pool size
			{10, 32, 16, 3, 1, tm::sparse | tm::read_random_order, "default"},

			// test with many threads pool size
			{10, 32, 64, 3, 9, tm::sparse | tm::read_random_order, "default"},
		};

		int ret = 0;
		for (auto const& t : tests)
			ret |= run_test(t);

		return ret;
	}

	// strip program name
	argc -= 1;
	argv += 1;
	test_case tc{20, 32, 16, 3, 10, test_mode::sparse, "default"};
	while (argc > 0)
	{
		lt::string_view opt(argv[0]);

		if (opt == "-h" || opt == "--help")
		{
			print_usage();
			return 0;
		}

		if (opt.substr(0, 1) == "-")
		{
			if (argc < 1)
			{
				std::cerr << "missing value associated with \"" << opt << "\"\n";
				print_usage();
				return 1;
			}
			if (opt == "-f")
				tc.num_files = std::atoi(argv[1]);
			else if (opt == "-q")
				tc.queue_size = std::atoi(argv[1]);
			else if (opt == "-t")
				tc.num_threads = std::atoi(argv[1]);
			else if (opt == "-r")
				tc.read_multiplier = std::atoi(argv[1]);
			else if (opt == "-p")
				tc.file_pool_size = std::atoi(argv[1]);
			else if (opt == "-d")
				tc.disk_backend = argv[1];
			else
			{
				std::cerr << "unknown option \"" << opt << "\"\n";
				print_usage();
				return 1;
			}

			argc -= 1;
			argv += 1;
		}
		else if (opt == "alloc")
			tc.flags &= ~test_mode::sparse;
		else if (opt == "even-size")
			tc.flags |= test_mode::even_file_sizes;
		else if (opt == "random-read")
			tc.flags |= test_mode::read_random_order;
		else if (opt == "flush")
			tc.flags |= test_mode::flush_files;
		else if (opt == "clear")
			tc.flags |= test_mode::clear_pieces;
		else
		{
			std::cerr << "unknown option \"" << opt << "\"\n";
			print_usage();
			return 1;
		}

		argc -= 1;
		argv += 1;
	}

	return run_test(tc);
}
