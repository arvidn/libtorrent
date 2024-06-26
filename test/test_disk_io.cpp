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
#include "libtorrent/sha1_hash.hpp"

using disk_test_mode_t = lt::flags::bitfield_flag<std::uint32_t, struct disk_test_mode_tag>;

namespace test_mode {
	using lt::operator ""_bit;
constexpr disk_test_mode_t v1 = 0_bit;
constexpr disk_test_mode_t v2 = 1_bit;
}

namespace {
void disk_io_test_suite(lt::disk_io_constructor_type disk_io
	, disk_test_mode_t const flags
	, int const piece_size
	, int const num_files)
{
	lt::io_context ios;
	lt::counters cnt;
	lt::settings_pack sett = lt::default_settings();
	std::unique_ptr<lt::disk_interface> disk_thread = disk_io(ios, sett, cnt);

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
	lt::storage_params params{
		fs,
		nullptr,
		name,
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
	int const block_size = std::min(lt::default_block_size, piece_size);
	for (lt::piece_index_t p : fs.piece_range())
	{
		int const len = fs.piece_size(p);
		std::vector<char> const buffer = generate_piece(p, len);
		for (int block = 0; block < len; block += block_size)
		{
			int const write_size = std::min(block_size, len - block);
			lt::disk_job_flags_t const disk_flags = (block + block_size >= len)
				? lt::disk_interface::flush_piece
				: lt::disk_job_flags_t{};
			std::cout << "flags: " << disk_flags << std::endl;
			disk_thread->async_write(storage
				, lt::peer_request{p, block, write_size}
				, buffer.data() + block
				, std::shared_ptr<lt::disk_observer>()
				, [&](lt::storage_error const& e) {
					TORRENT_ASSERT(!e.ec);
					++blocks_written;
				}
				, disk_flags);
			++expect_written;
			disk_thread->submit_jobs();
		}
	}

	std::cout << "blocks_written: " << blocks_written << std::endl;
	while (blocks_written < expect_written)
	{
		ios.run_for(std::chrono::milliseconds(500));
		std::cout << "blocks_written: " << blocks_written << std::endl;
	}

	TEST_EQUAL(blocks_written, expect_written);

	disk_thread->abort(true);
}

}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(test_mmap_disk_io_small_pieces)
{
	disk_io_test_suite(&lt::mmap_disk_io_constructor, test_mode::v1 | test_mode::v2, 300, 3);
}

TORRENT_TEST(test_mmap_disk_io)
{
	disk_io_test_suite(&lt::mmap_disk_io_constructor, test_mode::v1 | test_mode::v2, 0x8000, 3);
}
#endif

TORRENT_TEST(test_posix_disk_io)
{
	disk_io_test_suite(&lt::posix_disk_io_constructor, test_mode::v1 | test_mode::v2, 0x8000, 3);
}

TORRENT_TEST(test_pread_disk_io)
{
	disk_io_test_suite(&lt::pread_disk_io_constructor, test_mode::v1 | test_mode::v2, 0x8000, 3);
}

