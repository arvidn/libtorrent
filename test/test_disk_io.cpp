/*

Copyright (c) 2024, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring> // for std::memcmp
#include "test.hpp"
#include "disk_io_test.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"
#include "libtorrent/disk_interface.hpp"
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
#include "libtorrent/hasher.hpp"

using namespace std::chrono_literals;

using disk_test_mode_t = lt::flags::bitfield_flag<std::uint32_t, struct disk_test_mode_tag>;

namespace test_mode {
	using lt::operator ""_bit;
constexpr disk_test_mode_t v1 = 0_bit;
constexpr disk_test_mode_t v2 = 1_bit;
}

// Create a storage for the already-populated `fs` on `disk`. `fs` must outlive
// the returned handle (the storage keeps a reference to it); renamed_files and
// priorities are copied into the storage, so the helper can own them.
static lt::storage_holder add_test_torrent(lt::disk_interface& disk,
	lt::file_storage const& fs,
	char const* const save_path,
	bool const v1,
	bool const v2)
{
	lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
	lt::renamed_files rf;
	lt::storage_params const params{
		fs,
		rf,
		save_path,
		{},
		lt::storage_mode_t::storage_mode_sparse,
		priorities,
		lt::sha1_hash{},
		v1,
		v2,
	};
	return disk.new_torrent(params, std::shared_ptr<void>());
}

static void disk_io_test_suite_impl(lt::disk_io_constructor_type disk_io,
	disk_test_mode_t const flags,
	int const piece_size,
	int const num_files,
	int const hasher_threads,
	int const disk_threads,
	bool const raise_fence,
	bool const stacked_fence)
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

	lt::storage_holder storage = add_test_torrent(*disk_thread,
		fs,
		"test_torrent_store",
		bool(flags & test_mode::v1),
		bool(flags & test_mode::v2));

	int blocks_written = 0;
	int expect_written = 0;
	int hashes_done = 0;
	int expect_hashes = 0;
	int clears_done = 0;
	int expect_clears = 0;
	bool const need_v1 = bool(flags & test_mode::v1);
	bool const need_v2 = bool(flags & test_mode::v2);
	int const block_size = std::min(lt::default_block_size, piece_size);
	for (lt::piece_index_t p : fs.piece_range())
	{
		int const len = need_v1 ? fs.piece_size(p) : fs.piece_size2(p);
		std::vector<char> const buffer = generate_piece(p, len);

		// Raise a per-storage fence right before this piece's writes. Clearing
		// p while it is still empty is a no-op clear, but it raises the fence,
		// so the writes (and the hash) below are queued behind it and only
		// inserted/run when the clear lowers it. If the fence machinery ran the
		// hash before the queued writes landed in the cache, the hash would not
		// match. We deliberately don't submit_jobs() until the whole piece is
		// queued, so the fence stays up across all of p's writes.
		if (raise_fence || stacked_fence)
		{
			disk_thread->async_clear_piece(
				storage, p, [&clears_done](lt::piece_index_t) { ++clears_done; });
			++expect_clears;
		}
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

			// stacked-fence regression: right after this piece's first block,
			// raise a SECOND fence (a no-op clear on the next, still-empty piece)
			// before the rest of the piece is written. While both fences are up,
			// lowering the first reposts this partial piece's first block into the
			// cache ahead of the second (still-raised) fence. Nothing would flush
			// that partial piece, so before the fix the second fence waited on it
			// forever; the re-armed fence flush keeps it moving.
			if (stacked_fence && block == 0 && !last_block
				&& static_cast<int>(p) + 1 < fs.num_pieces())
			{
				lt::piece_index_t const next{static_cast<int>(p) + 1};
				disk_thread->async_clear_piece(
					storage, next, [&clears_done](lt::piece_index_t) { ++clears_done; });
				++expect_clears;
			}

			if (last_block)
			{
				// Issuing async_hash after a full piece is written ensures the
				// disk cache flushes the blocks (flush only happens once hashing
				// completes).
				int const blocks_in_piece = (len + block_size - 1) / block_size;
				auto v2_hashes = need_v2
					? std::make_shared<std::vector<lt::sha256_hash>>(std::size_t(blocks_in_piece))
					: std::make_shared<std::vector<lt::sha256_hash>>();
				lt::disk_job_flags_t const hash_flags = need_v1
					? lt::disk_interface::v1_hash
					: lt::disk_job_flags_t{};
				// expected hashes computed from the same generate_piece data.
				// v2 pieces don't span file boundaries, so v2 hashes only cover
				// the v2 portion (up to piece_size2) of the block buffer.
				std::vector<lt::sha256_hash> expected_v2;
				lt::sha1_hash expected_v1;
				if (need_v2)
				{
					expected_v2.resize(std::size_t(blocks_in_piece));
					int const v2_blocks = fs.blocks_in_piece2(p);
					int const v2_size = fs.piece_size2(p);
					for (int b = 0; b < v2_blocks; ++b)
					{
						int const off = b * block_size;
						int const bsize = std::min(block_size, v2_size - off);
						lt::hasher256 hh;
						hh.update({buffer.data() + off, bsize});
						expected_v2[std::size_t(b)] = hh.final();
					}
				}
				if (need_v1)
				{
					lt::hasher hh;
					hh.update({buffer.data(), len});
					expected_v1 = hh.final();
				}

				disk_thread->async_hash(storage,
					p,
					lt::span<lt::sha256_hash>(*v2_hashes),
					hash_flags | lt::disk_interface::flush_piece,
					[&hashes_done,
						p,
						v2_hashes,
						need_v1,
						need_v2,
						expected_v2 = std::move(expected_v2),
						expected_v1](lt::piece_index_t,
						lt::sha1_hash const& v1_hash,
						lt::storage_error const& e) {
						if (e.ec)
						{
							std::cout << "ERROR: failed to hash piece (p: " << p
								<< "): (" << e.ec.value()
								<< ") " << e.ec.message() << std::endl;
							std::abort();
						}
						if (need_v2)
						{
							TEST_EQUAL(v2_hashes->size(), expected_v2.size());
							for (std::size_t i = 0; i < expected_v2.size(); ++i)
								TEST_CHECK((*v2_hashes)[i] == expected_v2[i]);
						}
						if (need_v1) TEST_CHECK(v1_hash == expected_v1);
						++hashes_done;
					});
				++expect_hashes;
			}

			disk_thread->submit_jobs();
		}
	}

	auto const start_time = lt::aux::time_now();
#ifdef TORRENT_SIMULATE_SLOW_HASH
	auto const timeout = lt::seconds(30);
#else
	auto const timeout = lt::seconds(20);
#endif
	while (blocks_written < expect_written || hashes_done < expect_hashes
		|| clears_done < expect_clears)
	{
		ios.run_for(std::chrono::seconds(1));
		std::cout << "blocks_written: " << blocks_written << "/" << expect_written
				  << " hashes_done: " << hashes_done << "/" << expect_hashes
				  << " clears_done: " << clears_done << "/" << expect_clears << std::endl;

		if (lt::aux::time_now() - start_time > timeout)
		{
			TEST_ERROR("timeout");
			break;
		}
	}

	TEST_EQUAL(blocks_written, expect_written);
	TEST_EQUAL(hashes_done, expect_hashes);
	TEST_EQUAL(clears_done, expect_clears);

	disk_thread->abort(true);
}

static void disk_io_test_suite(lt::disk_io_constructor_type disk_io,
	int const num_files,
	bool const raise_fence = false,
	bool const stacked_fence = false)
{
	// posix_disk_io is single-threaded; disk_threads has no effect, so don't
	// re-run the same configuration twice.
	bool const single_threaded = is_single_threaded_disk_io(disk_io);
	for (disk_test_mode_t flags : {test_mode::v1, test_mode::v2, test_mode::v1 | test_mode::v2})
	{
		for (int hasher_threads : {0, 2})
		{
			for (int disk_threads : {0, 2})
			{
				if (single_threaded && disk_threads != 0) continue;
				for (int piece_size : {300, 0x8000})
				{
					disk_io_test_suite_impl(disk_io,
						flags,
						piece_size,
						num_files,
						hasher_threads,
						disk_threads,
						raise_fence,
						stacked_fence);
				}
			}
		}
	}
}

// Verify that async_hash2 returns the correct SHA-256 for a block that has
// been written but is still sitting in the disk cache (for pread_disk_io) /
// store buffer (for mmap_disk_io). With hashing_threads=0 the hasher kick is
// disabled, so no precomputed v2 block hash is stashed on the storage. With
// aio_threads=0 there is no thread to flush the cache to disk. In this state
// any hash2 implementation that falls through to a disk read would read zeros
// (the block has not been written to disk yet) and produce a wrong hash.
static void hash2_before_flush_impl(
	lt::disk_io_constructor_type disk_io, disk_test_mode_t const flags, int const piece_size)
{
	lt::io_context ios;
	lt::counters cnt;
	lt::settings_pack sett = lt::default_settings();
	sett.set_int(lt::settings_pack::hashing_threads, 0);
	sett.set_int(lt::settings_pack::aio_threads, 0);
	std::unique_ptr<lt::disk_interface> disk_thread = disk_io(ios, sett, cnt);

	std::cout << "hash2_before_flush: " << ((flags & test_mode::v1) ? "v1 " : "")
			  << ((flags & test_mode::v2) ? "v2 " : "") << " piece_size: " << piece_size
			  << std::endl;

	lt::file_storage fs;
	fs.set_piece_length(piece_size);
	int const file_size = piece_size * 3 + 17;
	fs.add_file("hash2_before_flush_torrent/file-0", file_size, {});
	fs.set_num_pieces(int((file_size + piece_size - 1) / piece_size));

	lt::storage_holder storage = add_test_torrent(*disk_thread,
		fs,
		"hash2_before_flush_store",
		bool(flags & test_mode::v1),
		bool(flags & test_mode::v2));

	int const block_size = std::min(lt::default_block_size, piece_size);
	bool const need_v1 = bool(flags & test_mode::v1);

	// only counts hash2 callbacks: write callbacks won't fire here because
	// nothing flushes the cache to disk.
	int hashes_done = 0;
	int hashes_expected = 0;
	bool any_mismatch = false;

	for (lt::piece_index_t p : fs.piece_range())
	{
		int const piece_size_v2 = fs.piece_size2(p);
		int const len = need_v1 ? fs.piece_size(p) : piece_size_v2;
		auto buffer = std::make_shared<std::vector<char>>(generate_piece(p, len));
		for (int off = 0; off < len; off += block_size)
		{
			int const write_size = std::min(block_size, len - off);
			disk_thread->async_write(
				storage,
				lt::peer_request{p, off, write_size},
				buffer->data() + off,
				std::shared_ptr<lt::disk_observer>(),
				[buffer](lt::storage_error const&) {
					// hold the buffer alive while a write is outstanding;
					// no other action needed
				},
				lt::disk_job_flags_t{});

			// only verify blocks inside the v2 piece (v2 pieces don't cross
			// file boundaries, so v1 padding past piece_size2 is not covered).
			if (off >= piece_size_v2) continue;

			int const v2_size = std::min(block_size, piece_size_v2 - off);
			lt::hasher256 hh;
			hh.update({buffer->data() + off, v2_size});
			lt::sha256_hash const expected = hh.final();

			disk_thread->async_hash2(storage,
				p,
				off,
				lt::disk_job_flags_t{},
				[&hashes_done, &any_mismatch, expected, p, off](
					lt::piece_index_t, lt::sha256_hash const& hash, lt::storage_error const& e) {
					if (e.ec)
					{
						std::cout << "ERROR: failed to hash2 (p: " << p << " off: " << off << "): ("
								  << e.ec.value() << ") " << e.ec.message() << std::endl;
						std::abort();
					}
					if (hash != expected)
					{
						std::cout << "MISMATCH at piece " << p << " offset " << off << ": expected "
								  << expected << " got " << hash << std::endl;
						any_mismatch = true;
					}
					++hashes_done;
				});
			++hashes_expected;

			disk_thread->submit_jobs();
		}
	}

	// also drain any cached blocks so the disk_io destructor's m_cache.size()
	// == 0 assert is happy. clear_piece aborts the queued write jobs (their
	// callbacks fire with cancel) and drops the cached buffers. The hash2
	// results were computed inline above, so issuing the clears now does not
	// affect the values the hash2 callbacks will deliver.
	int cleared = 0;
	int const num_pieces = static_cast<int>(fs.num_pieces());
	for (lt::piece_index_t p : fs.piece_range())
	{
		disk_thread->async_clear_piece(storage, p, [&cleared](lt::piece_index_t) { ++cleared; });
	}
	disk_thread->submit_jobs();

	auto const start_time = lt::aux::time_now();
	auto const timeout = lt::seconds(20);
	while (hashes_done < hashes_expected || cleared < num_pieces)
	{
		ios.run_for(std::chrono::seconds(1));
		if (lt::aux::time_now() - start_time > timeout)
		{
			TEST_ERROR("timeout");
			break;
		}
	}

	TEST_EQUAL(hashes_done, hashes_expected);
	TEST_EQUAL(cleared, num_pieces);
	TEST_CHECK(!any_mismatch);

	disk_thread->abort(true);
}

static void hash2_before_flush_suite(lt::disk_io_constructor_type disk_io)
{
	for (disk_test_mode_t flags : {test_mode::v2, test_mode::v1 | test_mode::v2})
	{
		for (int piece_size : {0x4000, 0x8000})
		{
			hash2_before_flush_impl(disk_io, flags, piece_size);
		}
	}
}

enum class read_case
{
	cache_miss, // neither block cached; the read is served from disk
	cache_hit, // both blocks cached; the read is served from the cache
	partial, // block 0 cached, block 1 on disk; served as a partial_read
	partial_fence // block 0 cached, block 1 queued behind a fence
};

// Exercises unaligned reads that cross a block boundary in pread_disk_io, in
// four cache states (read_case). The disk is seeded (phase 1) with bytes that
// differ from the cache content (phase 2, bit-inverted), so a read returns the
// expected bytes only when served from the intended place. Each block of the
// crossing read is verified against its own source (cache or disk), so the
// partial cases check the two halves are stitched together correctly.
//
// partial is the case prepare_read() turns into a partial_read: block 0 is in the
// cache and block 1 is only on disk, so the cached half is copied and the missing
// half is read from disk.
//
// partial_fence adds a storage fence: block 0 is cached and block 1 is a write
// queued behind the fence. The read is posted while the fence is up, so it is
// parked behind block 1's write. When the fence lowers, block 1's write is
// reposted into the cache ahead of the read, and prepare_read() (re-run on the
// unblocked read) must reflect it -- this is the ordering invariant a read
// resumed from a fence must observe writes resumed ahead of it.
static void unaligned_cross_block_read_impl(
	lt::disk_io_constructor_type disk_io, int const piece_size, read_case const rc)
{
	lt::io_context ios;
	lt::counters cnt;
	lt::settings_pack sett = lt::default_settings();
	sett.set_int(lt::settings_pack::hashing_threads, 0);
	sett.set_int(lt::settings_pack::aio_threads, 2);
	std::unique_ptr<lt::disk_interface> disk_thread = disk_io(ios, sett, cnt);

	int const block_size = std::min(lt::default_block_size, piece_size);
	TEST_CHECK(piece_size >= 2 * block_size);

	std::cout << "unaligned_cross_block_read: piece_size: " << piece_size << " case: " << int(rc)
			  << std::endl;

	int const num_test_pieces = 8;
	lt::file_storage fs;
	fs.set_piece_length(piece_size);
	// one extra, never-written piece to raise a no-op storage fence on.
	int const file_size = piece_size * (num_test_pieces + 1);
	fs.add_file("unaligned_read_torrent/file-0", file_size, {});
	fs.set_num_pieces(int((file_size + piece_size - 1) / piece_size));

	lt::storage_holder storage =
		add_test_torrent(*disk_thread, fs, "unaligned_read_store", true /*v1*/, false /*v2*/);

	lt::piece_index_t const fence_piece{num_test_pieces};

	auto const drive = [&ios](auto cond, char const* what) {
		auto const start = lt::aux::time_now();
		while (cond())
		{
			ios.run_for(1s);
			if (lt::aux::time_now() - start > 20s)
			{
				TEST_ERROR(what);
				break;
			}
		}
	};

	// phase 1: write each test piece fully and flush it to disk, so the disk
	// holds the correct bytes as a fallback.
	int hashes_done = 0;
	int hashes_expected = 0;
	int p1_writes_done = 0;
	int p1_writes_expected = 0;
	for (lt::piece_index_t const p : fs.piece_range())
	{
		if (p == fence_piece) continue;
		int const len = fs.piece_size(p);
		// async_write copies the buffer synchronously, so a local outlives the call.
		std::vector<char> const buffer = generate_piece(p, len);
		for (int off = 0; off < len; off += block_size)
		{
			int const ws = std::min(block_size, len - off);
			disk_thread->async_write(
				storage,
				lt::peer_request{p, off, ws},
				buffer.data() + off,
				std::shared_ptr<lt::disk_observer>(),
				[&p1_writes_done](lt::storage_error const& e) {
					TEST_CHECK(!e.ec);
					++p1_writes_done;
				},
				lt::disk_job_flags_t{});
			++p1_writes_expected;
		}
		disk_thread->async_hash(storage,
			p,
			lt::span<lt::sha256_hash>{},
			lt::disk_interface::v1_hash | lt::disk_interface::flush_piece,
			[&hashes_done](lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const& e) {
				TEST_CHECK(!e.ec);
				++hashes_done;
			});
		++hashes_expected;
		disk_thread->submit_jobs();
	}
	drive([&] { return hashes_done < hashes_expected; }, "timeout (phase 1)");
	// wait until every phase-1 block is on disk (write callback fires after pwrite).
	drive([&] { return p1_writes_done < p1_writes_expected; }, "timeout (phase 1 writes)");
	// clear phase-1 cache entries before phase 2 writes to the same pieces.
	// with hashing_threads=0, async_hash's fast path sets piece_hash_returned_flag
	// without flushing; the flag stays in the cache until the generic thread evicts
	// the entry, which may not happen before phase 2's insert() hits the assert.
	int p1_clears_done = 0;
	for (lt::piece_index_t const p : fs.piece_range())
	{
		if (p == fence_piece) continue;
		disk_thread->async_clear_piece(
			storage, p, [&p1_clears_done](lt::piece_index_t) { ++p1_clears_done; });
	}
	disk_thread->submit_jobs();
	drive([&] { return p1_clears_done < num_test_pieces; }, "timeout (inter-phase drain)");

	// phase 2: put each piece's cache into the state under test, then issue an
	// unaligned read crossing the block 0/block 1 boundary and verify the bytes.
	// For partial_fence, whether an iteration actually parks the read behind the
	// fence is timing dependent, so iterate several pieces to reliably hit the
	// path.
	int reads_done = 0;
	int reads_expected = 0;
	int clears_done = 0;
	int clears_expected = 0;
	for (lt::piece_index_t const p : fs.piece_range())
	{
		if (p == fence_piece) continue;
		int const len = fs.piece_size(p);
		// async_write copies the buffer synchronously (even when the write is
		// queued behind a fence), so these locals don't need to outlive the call.
		std::vector<char> const disk_bytes = generate_piece(p, len);
		// bit-invert so cached blocks differ from the phase 1 bytes on disk.
		std::vector<char> cache_bytes = disk_bytes;
		for (char& c : cache_bytes)
			c = char(~c);

		// block 0 is cached in every case except cache_miss. block 1 is cached
		// only when both blocks are (cache_hit) or it is the write queued behind
		// the fence (partial_fence); for partial it stays on disk so the read
		// becomes a partial_read.
		bool const block0_cached = (rc != read_case::cache_miss);
		bool const block1_cached = (rc == read_case::cache_hit || rc == read_case::partial_fence);

		if (block0_cached)
			// block 0 -> cache (inserted synchronously). For partial_fence this is
			// also the outstanding write that keeps the fence up below.
			disk_thread->async_write(
				storage,
				lt::peer_request{p, 0, block_size},
				cache_bytes.data(),
				std::shared_ptr<lt::disk_observer>(),
				[](lt::storage_error const&) {},
				lt::disk_job_flags_t{});

		if (rc == read_case::partial_fence)
		{
			// raise a storage fence; block 0's write is outstanding so it stays up
			disk_thread->async_clear_piece(
				storage, fence_piece, [&clears_done](lt::piece_index_t) { ++clears_done; });
			++clears_expected;
		}

		if (block1_cached)
			// block 1 -> cache directly (cache_hit) or queued behind the fence
			// (partial_fence, where its write is reposted into the cache when the
			// fence lowers, ahead of the parked read)
			disk_thread->async_write(
				storage,
				lt::peer_request{p, block_size, block_size},
				cache_bytes.data() + block_size,
				std::shared_ptr<lt::disk_observer>(),
				[](lt::storage_error const&) {},
				lt::disk_job_flags_t{});

		// unaligned read [start, start + block_size) spans block 0 and block 1.
		// Each half is verified against its own source: a cached block reads back
		// the inverted bytes, a disk-only block the phase 1 bytes.
		int const start = block_size / 2;
		int const length = block_size;
		std::vector<char> expected(static_cast<std::size_t>(length));
		for (int i = 0; i < length; ++i)
		{
			int const off = start + i;
			bool const from_cache = (off < block_size) ? block0_cached : block1_cached;
			expected[std::size_t(i)] = (from_cache ? cache_bytes : disk_bytes)[std::size_t(off)];
		}
		disk_thread->async_read(storage,
			lt::peer_request{p, start, length},
			[&reads_done, expected = std::move(expected)](
				lt::disk_buffer_holder b, lt::storage_error const& e) {
				TEST_CHECK(!e.ec);
				TEST_CHECK(std::memcmp(b.data(), expected.data(), expected.size()) == 0);
				++reads_done;
			});
		++reads_expected;
		disk_thread->submit_jobs();
	}
	drive([&] { return reads_done < reads_expected || clears_done < clears_expected; },
		"timeout (phase 2)");

	TEST_EQUAL(reads_done, reads_expected);

	// drain the cached blocks so the destructor's empty-cache assert holds
	int drained = 0;
	for (lt::piece_index_t const p : fs.piece_range())
	{
		if (p == fence_piece) continue;
		disk_thread->async_clear_piece(storage, p, [&drained](lt::piece_index_t) { ++drained; });
	}
	disk_thread->submit_jobs();
	drive([&] { return drained < num_test_pieces; }, "timeout (drain)");

	disk_thread->abort(true);
}

#ifdef TORRENT_SIMULATE_SLOW_WRITE
// Regression test for a self-deadlock in pread_disk_io. When an
// async_clear_piece arrives while its piece is mid-flush, the clear is parked
// on the cached_piece_entry and dispatched from inside
// disk_cache::flush_piece_impl with the cache mutex held. pread_disk_io's
// clear-piece callback re-enters the cache (clear_piece_jobs ->
// add_completed_jobs -> schedule_flush -> disk_cache::flush_request), which
// re-locks that same (non-recursive) mutex and hangs the disk thread.
//
// The race is only deterministic with a widened flush window, so this test is
// only built (and only meaningful) in a simulate-slow=write configuration: the
// 100ms sleep in pread_storage::write keeps flushing_flag set long enough for
// the clear to reliably land mid-flush. Needs >= 2 disk threads -- one holds
// flushing_flag in the slow write while another services the clear-piece fence
// job and parks it.
static void clear_during_flush_impl(
	lt::disk_io_constructor_type disk_io, disk_test_mode_t const flags, int const piece_size)
{
	lt::io_context ios;
	lt::counters cnt;
	lt::settings_pack sett = lt::default_settings();
	sett.set_int(lt::settings_pack::hashing_threads, 2);
	sett.set_int(lt::settings_pack::aio_threads, 2);
	std::unique_ptr<lt::disk_interface> disk_thread = disk_io(ios, sett, cnt);

	std::cout << "clear_during_flush: " << ((flags & test_mode::v1) ? "v1 " : "")
			  << ((flags & test_mode::v2) ? "v2 " : "") << " piece_size: " << piece_size
			  << std::endl;

	lt::file_storage fs;
	fs.set_piece_length(piece_size);
	int const file_size = piece_size * 4;
	fs.add_file("clear_during_flush_torrent/file-0", file_size, {});
	fs.set_num_pieces(int((file_size + piece_size - 1) / piece_size));

	lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
	std::string const name = "clear_during_flush_store";
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

	lt::storage_holder storage = disk_thread->new_torrent(params, std::shared_ptr<void>());

	bool const need_v1 = bool(flags & test_mode::v1);
	bool const need_v2 = bool(flags & test_mode::v2);
	int const block_size = std::min(lt::default_block_size, piece_size);

	int blocks_written = 0;
	int expect_written = 0;
	int hashes_done = 0;
	int expect_hashes = 0;
	int clears_done = 0;
	int expect_clears = 0;

	for (lt::piece_index_t p : fs.piece_range())
	{
		int const len = need_v1 ? fs.piece_size(p) : fs.piece_size2(p);
		std::vector<char> const buffer = generate_piece(p, len);
		for (int block = 0; block < len; block += block_size)
		{
			int const write_size = std::min(block_size, len - block);
			bool const last_block = (block + block_size >= len);
			lt::disk_job_flags_t const disk_flags =
				last_block ? lt::disk_interface::flush_piece : lt::disk_job_flags_t{};
			disk_thread->async_write(
				storage,
				lt::peer_request{p, block, write_size},
				buffer.data() + block,
				std::shared_ptr<lt::disk_observer>(),
				[&blocks_written](lt::storage_error const&) { ++blocks_written; },
				disk_flags);
			++expect_written;

			if (last_block)
			{
				int const blocks_in_piece = (len + block_size - 1) / block_size;
				auto v2_hashes = need_v2
					? std::make_shared<std::vector<lt::sha256_hash>>(std::size_t(blocks_in_piece))
					: std::make_shared<std::vector<lt::sha256_hash>>();
				lt::disk_job_flags_t const hash_flags =
					(need_v1 ? lt::disk_interface::v1_hash : lt::disk_job_flags_t{})
					| lt::disk_interface::flush_piece;
				disk_thread->async_hash(storage,
					p,
					lt::span<lt::sha256_hash>(*v2_hashes),
					hash_flags,
					[&hashes_done, v2_hashes](lt::piece_index_t,
						lt::sha1_hash const&,
						lt::storage_error const&) { ++hashes_done; });
				++expect_hashes;
			}
		}
		disk_thread->submit_jobs();

		// give a disk thread time to enter the slow flush for this piece, so
		// the clear below is observed while flushing_flag is set and gets
		// parked on the entry (the path that used to deadlock).
		std::this_thread::sleep_for(std::chrono::milliseconds(60));

		disk_thread->async_clear_piece(
			storage, p, [&clears_done](lt::piece_index_t) { ++clears_done; });
		++expect_clears;
		disk_thread->submit_jobs();
	}

	auto const start_time = lt::aux::time_now();
	auto const timeout = lt::seconds(20);
	while (blocks_written < expect_written || hashes_done < expect_hashes
		|| clears_done < expect_clears)
	{
		ios.run_for(std::chrono::seconds(1));
		std::cout << "blocks_written: " << blocks_written << "/" << expect_written
				  << " hashes_done: " << hashes_done << "/" << expect_hashes
				  << " clears_done: " << clears_done << "/" << expect_clears << std::endl;

		if (lt::aux::time_now() - start_time > timeout)
		{
			TEST_ERROR("timeout (likely deadlock in clear-during-flush path)");
			break;
		}
	}

	TEST_EQUAL(blocks_written, expect_written);
	TEST_EQUAL(hashes_done, expect_hashes);
	TEST_EQUAL(clears_done, expect_clears);

	disk_thread->abort(true);
}

static void clear_during_flush_suite(lt::disk_io_constructor_type disk_io)
{
	for (disk_test_mode_t flags : {test_mode::v1, test_mode::v2, test_mode::v1 | test_mode::v2})
	{
		for (int piece_size : {0x4000, 0x8000})
		{
			clear_during_flush_impl(disk_io, flags, piece_size);
		}
	}
}
#endif // TORRENT_SIMULATE_SLOW_WRITE

// Exercises dispatching a deferred clear_piece from inside the cache flush.
// Only deterministic (and only built) under simulate-slow=write; see
// clear_during_flush_impl.
TORRENT_TEST_DISK_IO(test_disk_io_clear_during_flush)
{
#ifdef TORRENT_SIMULATE_SLOW_WRITE
	clear_during_flush_suite(disk_io);
#else
	(void)disk_io;
#endif
}

TORRENT_TEST_DISK_IO(test_disk_io) { disk_io_test_suite(disk_io, 3); }

// same as test_pread_disk_io, but raises a fence (a no-op async_clear_piece)
// before each piece's writes, so the writes and the hash are queued behind the
// fence. This exercises that a piece's hash still reflects every write posted
// before it once the fence is lowered.
TORRENT_TEST_DISK_IO(test_pread_disk_io_fence) { disk_io_test_suite(disk_io, 3, true); }

TORRENT_TEST_DISK_IO(test_disk_io_hash2_before_flush) { hash2_before_flush_suite(disk_io); }

// pread_disk_io only: prepare_read()'s partial_read path and the fence/write-back
// cache forward-progress the partial_fence case exercises are specific to that
// backend.
TORRENT_TEST(disk_io_unaligned_read_cache_miss_pread)
{
	unaligned_cross_block_read_impl(lt::pread_disk_io_constructor, 0x8000, read_case::cache_miss);
}

TORRENT_TEST(disk_io_unaligned_read_cache_hit_pread)
{
	unaligned_cross_block_read_impl(lt::pread_disk_io_constructor, 0x8000, read_case::cache_hit);
}

TORRENT_TEST(disk_io_partial_read_pread)
{
	unaligned_cross_block_read_impl(lt::pread_disk_io_constructor, 0x8000, read_case::partial);
}

TORRENT_TEST(disk_io_partial_read_fence_pread)
{
	unaligned_cross_block_read_impl(
		lt::pread_disk_io_constructor, 0x8000, read_case::partial_fence);
}

// like test_pread_disk_io_fence, but raises a SECOND, stacked fence in the
// middle of each piece (after its first block), leaving a partial piece queued
// between two fences. Exercises forward progress when a stacked fence is
// re-raised over the writes reposted by the fence ahead of it.
TORRENT_TEST_DISK_IO(test_pread_disk_io_stacked_fence)
{
	disk_io_test_suite(disk_io, 3, false, true);
}
