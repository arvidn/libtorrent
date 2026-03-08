/*

Copyright (c) 2024, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>
#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"
#include "libtorrent/pread_disk_io.hpp"
#include "libtorrent/session_params.hpp" // for disk_io_constructor_type
#include "libtorrent/settings_pack.hpp" // for default_settings
#include "libtorrent/flags.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/sha1_hash.hpp"

using disk_test_mode_t = lt::flags::bitfield_flag<std::uint32_t, struct disk_test_mode_tag>;

namespace test_mode {
	using lt::operator ""_bit;
constexpr disk_test_mode_t v1 = 0_bit;
constexpr disk_test_mode_t v2 = 1_bit;
}

namespace {
void disk_io_test_suite_impl(lt::disk_io_constructor_type disk_io
	, disk_test_mode_t const flags
	, int const piece_size
	, int const num_files
	, int const hasher_threads
	, int const disk_threads)
{
	lt::io_context ios;
	lt::counters cnt;
	lt::settings_pack sett = lt::default_settings();
	sett.set_int(lt::settings_pack::hashing_threads, hasher_threads);
	sett.set_int(lt::settings_pack::aio_threads, disk_threads);
	std::unique_ptr<lt::disk_interface> disk_thread = disk_io(ios, sett, cnt);

	std::cout << "disk_io_test_suite: "
		<< ((flags & test_mode::v1) ? "v1 " : "")
		<< ((flags & test_mode::v2) ? "v2 " : "")
		<< " disk-threads: " << disk_threads
		<< " hasher_threads: " << hasher_threads
		<< " num-files: " << num_files
		<< " piece_size: " << piece_size
		<< std::endl;

	lt::file_storage fs;
	fs.set_piece_length(piece_size);
	std::int64_t total_size = 0;
	for (int i = 0; i < num_files; ++i)
	{
		int const file_size = piece_size * 2 + i * 11;
		total_size += file_size;
		fs.add_file("test-torrent/file-" + std::to_string(i), file_size, {});
	}
	fs.set_num_pieces(int((total_size + piece_size - 1) / piece_size));

	lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
	std::string const name = "test_torrent_store";
	lt::renamed_files rf;
	lt::storage_params params{
		fs,
		rf,
		name,
		{},
		lt::storage_mode_t::storage_mode_sparse,
		priorities,
		lt::sha1_hash{},
		bool(flags & test_mode::v1),
		bool(flags & test_mode::v2),
	};

	lt::storage_holder storage = disk_thread->new_torrent(params
		, std::shared_ptr<void>());

	int blocks_written = 0;
	int expect_written = 0;
	int hashes_done = 0;
	int expect_hashes = 0;
	bool const need_v1 = bool(flags & test_mode::v1);
	bool const need_v2 = bool(flags & test_mode::v2);
	int const block_size = std::min(lt::default_block_size, piece_size);
	for (lt::piece_index_t p : fs.piece_range())
	{
		int const len = fs.piece_size(p);
		std::vector<char> const buffer = generate_piece(p, len);
		for (int block = 0; block < len; block += block_size)
		{
			int const write_size = std::min(block_size, len - block);
			bool const last_block = (block + block_size >= len);
			lt::disk_job_flags_t const disk_flags = last_block
				? lt::disk_interface::flush_piece
				: lt::disk_job_flags_t{};
			disk_thread->async_write(storage
				, lt::peer_request{p, block, write_size}
				, buffer.data() + block
				, std::shared_ptr<lt::disk_observer>()
				, [&](lt::storage_error const& e) {
					if (e.ec) {
						std::cout << "ERROR: failed to write block (p: " << p
							<< " b: " << block
							<< " s: " << write_size
							<< "): (" << e.ec.value()
							<< ") " << e.ec.message() << std::endl;
							std::abort();
					}
					++blocks_written;
				}
				, disk_flags);
			++expect_written;

			if (last_block)
			{
				// Issuing async_hash after a full piece is written ensures the
				// disk cache flushes the blocks (flush only happens once hashing
				// completes).
				int const blocks_in_piece = (len + block_size - 1) / block_size;
				auto v2_hashes = need_v2
					? std::make_shared<std::vector<lt::sha256_hash>>(blocks_in_piece)
					: std::make_shared<std::vector<lt::sha256_hash>>();
				lt::disk_job_flags_t const hash_flags = need_v1
					? lt::disk_interface::v1_hash
					: lt::disk_job_flags_t{};
				disk_thread->async_hash(storage, p
					, lt::span<lt::sha256_hash>(*v2_hashes)
					, hash_flags
					, [&hashes_done, p, v2_hashes](lt::piece_index_t, lt::sha1_hash const&
						, lt::storage_error const& e) {
						if (e.ec) {
							std::cout << "ERROR: failed to hash piece (p: " << p
								<< "): (" << e.ec.value()
								<< ") " << e.ec.message() << std::endl;
							std::abort();
						}
						++hashes_done;
					});
				++expect_hashes;
			}

			disk_thread->submit_jobs();
		}
	}

	auto const start_time = lt::aux::time_now();
	while (blocks_written < expect_written || hashes_done < expect_hashes)
	{
		ios.run_for(std::chrono::milliseconds(500));
		std::cout << "blocks_written: " << blocks_written << "/" << expect_written
			<< " hashes_done: " << hashes_done << "/" << expect_hashes << std::endl;

		if (lt::aux::time_now() - start_time > lt::seconds(20))
		{
			TEST_ERROR("timeout");
			break;
		}
	}

	TEST_EQUAL(blocks_written, expect_written);
	TEST_EQUAL(hashes_done, expect_hashes);

	disk_thread->abort(true);
}

void disk_io_test_suite(lt::disk_io_constructor_type disk_io
	, int const num_files)
{
	for (disk_test_mode_t flags : {test_mode::v1, test_mode::v2, test_mode::v1 | test_mode::v2})
	{
		for (int hasher_threads : {0, 2})
		{
			for (int disk_threads : {0, 2})
			{
				for (int piece_size : {300, 0x8000})
				{
					disk_io_test_suite_impl(disk_io
						, flags
						, piece_size
						, num_files
						, hasher_threads
						, disk_threads);
				}
			}
		}
	}
}

}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(test_mmap_disk_io)
{
	disk_io_test_suite(&lt::mmap_disk_io_constructor, 3);
}

#endif

TORRENT_TEST(test_posix_disk_io)
{
	disk_io_test_suite(&lt::posix_disk_io_constructor, 3);
}

TORRENT_TEST(test_pread_disk_io)
{
	disk_io_test_suite(&lt::pread_disk_io_constructor, 3);
}
