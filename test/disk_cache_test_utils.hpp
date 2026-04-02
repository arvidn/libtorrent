/*

Copyright (c) 2024-2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Shared test infrastructure for disk_cache unit tests and slow-hash tests.

#ifndef TORRENT_DISK_CACHE_TEST_UTILS_HPP_INCLUDED
#define TORRENT_DISK_CACHE_TEST_UTILS_HPP_INCLUDED

#include "libtorrent/aux_/disk_cache.hpp"
#include "libtorrent/aux_/pread_disk_job.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp" // jobqueue_t
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/disk_interface.hpp"  // default_block_size
#include "libtorrent/io_context.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/units.hpp"
#include <cstring>
#include <memory>
#include <vector>

using test_mode_t = lt::flags::bitfield_flag<std::uint8_t, struct test_mode_tag>;
namespace test_mode {
	using lt::operator ""_bit;
	constexpr test_mode_t v1 = 0_bit;
	constexpr test_mode_t v2 = 1_bit;
}

struct test_allocator : lt::buffer_allocator_interface
{
	void free_disk_buffer(char* b) override { delete[] b; --live; }
	void free_multiple_buffers(lt::span<char*> bufs) override
	{
		for (char* b : bufs) free_disk_buffer(b);
	}

	lt::disk_buffer_holder alloc(int const size = lt::default_block_size)
	{
		++live;
		return lt::disk_buffer_holder(*this, new char[static_cast<size_t>(size)], size);
	}

	virtual ~test_allocator() = default;

	int live = 0;
};

// drives disk_cache directly without any storage layer.
// The piece metadata (piece_size, piece_size2, v1, v2) is specified in the
// constructor and passed straight to disk_cache::insert() via
// piece_entry_params - no file_storage or pread_storage involved.
struct cache_fixture
{
	lt::io_context ios;
	lt::aux::disk_cache cache{ios};
	test_allocator alloc;
	std::vector<std::unique_ptr<lt::aux::pread_disk_job>> live_jobs;

	test_mode_t const mode;
	// v1 effective piece size (determines blocks_in_piece)
	int const piece_size;
	// v2 piece size (may differ from piece_size for hybrid torrents)
	int const piece_size2;

	// piece_size defaults to blocks_ * default_block_size;
	// piece_size2 defaults to piece_size when not specified.
	cache_fixture(int const blocks_, test_mode_t const mode_
		, int const piece_size_ = 0, int const piece_size2_ = 0)
		: mode(mode_)
		, piece_size(piece_size_ > 0 ? piece_size_ : blocks_ * lt::default_block_size)
		, piece_size2(piece_size2_ > 0 ? piece_size2_ : (piece_size_ > 0 ? piece_size_ : blocks_ * lt::default_block_size))
	{
		TORRENT_ASSERT(mode & (test_mode::v1 | test_mode::v2));
		cache.set_max_size(1024); // generous; no back-pressure by default
	}

	lt::aux::piece_location loc(lt::piece_index_t const p) const
	{
		return {lt::storage_index_t{0}, p};
	}

	lt::aux::disk_cache::piece_entry_params piece_params() const
	{
		return {
			piece_size2,
			piece_size,
			bool(mode & test_mode::v1), bool(mode & test_mode::v2)
		};
	}

	// Allocate a write-job whose buffer is filled with fill_char.
	// The buffer is sized to the actual block size (which may be less than
	// default_block_size for the last block of a piece with unaligned piece_size).
	// Ownership is transferred to live_jobs; the raw pointer is returned.
	// write_job->storage is intentionally null: disk_cache no longer needs it.
	lt::aux::pread_disk_job* make_write_job(
		lt::piece_index_t const piece
		, int const block
		, char const fill
		, int const buf_size)
	{
		auto j = std::make_unique<lt::aux::pread_disk_job>();
		auto buf = alloc.alloc(buf_size);
		std::memset(buf.data(), fill, std::size_t(buf.size()));
		j->action = lt::aux::job::write{
			{},
			std::move(buf),
			piece,
			block * lt::default_block_size,
			static_cast<std::uint16_t>(buf_size)
		};
		auto* ret = j.get();
		live_jobs.push_back(std::move(j));
		return ret;
	}

	lt::aux::insert_result_flags insert(
		lt::piece_index_t const piece
		, int const block
		, bool const force_flush = false
		, char const fill = 0x5a)
	{
		return cache.insert(loc(piece), block, force_flush, nullptr
			, make_write_job(piece, block, fill, lt::default_block_size)
			, piece_params());
	}

	lt::aux::insert_result_flags insert(
		lt::piece_index_t const piece
		, int const block
		, lt::aux::disk_cache::piece_entry_params const& params
		, int const buf_size
		, bool const force_flush = false
		, char const fill = 0x5a)
	{
		return cache.insert(loc(piece), block, force_flush, nullptr
			, make_write_job(piece, block, fill, buf_size)
			, params);
	}

	// Simulate flushing: marks every block that has a write_job as flushed.
	// Returns the number of blocks flushed.
	int flush(int const target = 0, bool const optimistic = false)
	{
		int total = 0;
		cache.flush_to_disk(
			[&](lt::bitfield& flushed, lt::span<lt::aux::cached_block_entry const> blocks) -> int {
				int count = 0;
				for (int i = 0; i < int(blocks.size()); ++i)
				{
					if (!blocks[i].get_write_job()) continue;
					flushed.set_bit(i);
					++count;
				}
				total += count;
				return count;
			},
			target,
			[](lt::jobqueue_t, lt::aux::disk_job*) {}
			, optimistic);
		return total;
	}

	// Advance the hasher for all pending pieces, discarding completed jobs.
	void kick_hashers()
	{
		lt::jobqueue_t completed, retry;
		cache.kick_pending_hashers(completed, retry);
	}

	// Simulate flush_storage(): marks every block with a write_job as flushed.
	void flush_storage_for(lt::storage_index_t const storage = lt::storage_index_t{0})
	{
		cache.flush_storage(
			[](lt::bitfield& flushed, lt::span<lt::aux::cached_block_entry const> blocks) -> int {
				int count = 0;
				for (int i = 0; i < int(blocks.size()); ++i)
				{
					if (!blocks[i].get_write_job()) continue;
					flushed.set_bit(i);
					++count;
				}
				return count;
			},
			storage,
			[](lt::jobqueue_t, lt::aux::disk_job*) {});
	}

};

#endif // TORRENT_DISK_CACHE_TEST_UTILS_HPP_INCLUDED
