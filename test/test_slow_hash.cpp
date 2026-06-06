/*

Copyright (c) 2024-2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Tests that require TORRENT_SIMULATE_SLOW_HASH to produce reliable results.
// Build and run this file in a slow-hash configuration to exercise the
// concurrent hash-job retry path without making the rest of the test suite
// take many minutes.

#ifndef TORRENT_SIMULATE_SLOW_HASH

#include "test.hpp"
TORRENT_TEST(slow_hash_tests_skipped) {}

#else // TORRENT_SIMULATE_SLOW_HASH

#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include "test.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"
#include "disk_cache_test_utils.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"
#include "libtorrent/pread_disk_io.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"

using lt::span;
using namespace lt;
using namespace lt::aux;

namespace {

// Exercise the retry path in the disk I/O thread loop: when kick_hasher stalls
// mid-piece (block 1 absent from cache) while a hash job is parked on the piece
// with in_progress set, the job must be re-queued directly — not via add_job(),
// which asserts that in_progress is clear.
//
// Setup (aio_threads=1, hashing_threads=2):
//   1. Pre-create the file with the full piece data on disk so the retried hash
//      job can read block 1.  Block 1 is never inserted into the cache.
//   2. Call async_hash before inserting any block: the piece is not in the cache,
//      so try_hash_piece() returns post_job and the job is enqueued via add_job()
//      with in_progress=true.
//   3. Write block 0 without flush_piece -> its buffer stays in cache.
//      insert() sets needs_hasher_kick_flag and calls interrupt() on the hash pool,
//      waking one hash thread (A) which runs kick_pending_hashers -> kick_hasher.
//   4. submit_jobs() wakes hash thread B, which picks up the hash job, calls
//      do_job(hash) -> hash_piece -> sees hashing_flag set by A -> parks the job
//      (in_progress is still set) in e.hash_job and returns job_deferred.
//   5. Hash thread A finishes block 0 (slow, ~1.6 s), checks block 1 -> absent
//      -> stalls, extracts the parked job into retry_jobs.
//      Old code: add_job(j, false) asserts.  New code: push_back works.
//   6. The retried hash job reads block 1 from the pre-created file and completes.
void hash_job_retry_test(lt::disk_io_constructor_type disk_io
	, std::string const& save_path)
{
	lt::io_context ios;
	lt::counters cnt;
	lt::settings_pack sett = lt::default_settings();
	sett.set_int(lt::settings_pack::hashing_threads, 2);
	sett.set_int(lt::settings_pack::aio_threads, 1);
	std::unique_ptr<lt::disk_interface> disk_thread = disk_io(ios, sett, cnt);

	int const piece_size = 2 * lt::default_block_size;
	lt::file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file("test-retry/file-0", piece_size, {});
	fs.set_num_pieces(1);

	// Step 1: pre-create the file so the retried hash job can read block 1.
	// Block 1 is never inserted into the cache; kick_hasher hashes block 0 from
	// cache, stalls on block 1, and the retried hash job reads block 1 from disk.
	std::vector<char> const piece_buf = generate_piece(lt::piece_index_t(0), piece_size);
	{
		lt::error_code ec;
		lt::create_directory(save_path, ec);
		if (ec && ec != boost::system::errc::file_exists)
			std::cout << "ERROR: create_directory(" << save_path << "): " << ec.message() << "\n";
		std::string const dir = save_path + "/test-retry";
		lt::create_directory(dir, ec);
		if (ec && ec != boost::system::errc::file_exists)
			std::cout << "ERROR: create_directory(" << dir << "): " << ec.message() << "\n";
		std::ofstream f(dir + "/file-0", std::ios::binary);
		if (!f)
			std::cout << "ERROR: failed to open " << dir << "/file-0\n";
		f.write(piece_buf.data(), piece_size);
	}

	lt::aux::vector<lt::download_priority_t, lt::file_index_t> priorities;
	lt::renamed_files rf;
	lt::storage_params params{
		fs,
		rf,
		save_path,
		{},
		lt::storage_mode_t::storage_mode_sparse,
		priorities,
		lt::sha1_hash{},
		true,  // v1
		false, // v2
	};

	lt::storage_holder storage = disk_thread->new_torrent(params
		, std::shared_ptr<void>());

	int hashes_done = 0;

	// Step 2: issue async_hash before inserting any block.
	// The piece is not yet in the cache so try_hash_piece() returns post_job,
	// and the job is dispatched via add_job() with in_progress=true.
	std::vector<lt::sha256_hash> v2_hashes;
	disk_thread->async_hash(storage, lt::piece_index_t(0)
		, lt::span<lt::sha256_hash>(v2_hashes)
		, lt::disk_interface::v1_hash | lt::disk_interface::flush_piece
		, [&](lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const& e) {
			if (e.ec)
				std::cout << "ERROR: hash failed: " << e.ec.message() << "\n";
			++hashes_done;
		});

	// Step 3: write block 0 without flush_piece so its buffer stays in cache.
	// insert() sets needs_hasher_kick_flag and calls interrupt() on the hash pool,
	// waking hash thread A to run kick_pending_hashers -> kick_hasher (slow).
	disk_thread->async_write(storage
		, lt::peer_request{lt::piece_index_t(0), 0, lt::default_block_size}
		, piece_buf.data()
		, std::shared_ptr<lt::disk_observer>()
		, [](lt::storage_error const&) {}
		, lt::disk_job_flags_t{});

	// Step 4: wake hash thread B so it can pick up the hash job and park it.
	disk_thread->submit_jobs();

	auto const start_time = lt::aux::time_now();
	auto const timeout = lt::seconds(30);
	while (hashes_done < 1)
	{
		ios.run_for(std::chrono::seconds(1));
		std::cout << "hashes_done: " << hashes_done << "/1\n";
		if (lt::aux::time_now() - start_time > timeout)
		{
			TEST_ERROR("timeout");
			break;
		}
	}

	TEST_EQUAL(hashes_done, 1);

	disk_thread->abort(true);
}

// try_hash_piece is called while kick_pending_hashers is mid-run:
// the hash job is hung on the piece (job_queued) and dispatched by the hasher
// once it completes. This exercises the concurrent path in kick_hasher where
// hash_job is non-null when hashing finishes.
void test_hash_job_dispatched_by_hasher(test_mode_t const mode)
{
	cache_fixture f(1, mode);
	f.insert(0_piece, 0);

	std::vector<sha256_hash> block_hashes(bool(mode & test_mode::v2) ? 1 : 0);
	auto hash_job = std::make_unique<pread_disk_job>();
	hash_job->action = job::hash{
		{}, 0_piece,
		span<sha256_hash>{block_hashes.data(), int(block_hashes.size())},
		sha1_hash{}
	};

	jobqueue_t completed, retry;

	// With slow hashing each block takes ~1.6 s. Run both the v1 hasher
	// (kick_pending_hashers) and the v2 drain on the same thread, mirroring
	// pread_disk_io::thread_fun, so a v2-only piece has a hashing window
	// too. Call try_hash_piece after a short sleep so the hasher is
	// reliably mid-run.
	std::thread t([&]() {
		f.cache.kick_pending_hashers(completed, retry);
		f.cache.drain_v2_hash_queue(
			[&block_hashes](std::shared_ptr<lt::aux::pread_storage> const&,
				lt::piece_index_t,
				int const block,
				lt::sha256_hash const& h) {
				if (block < int(block_hashes.size())) block_hashes[std::size_t(block)] = h;
			},
			retry,
			[](jobqueue_t, disk_job*) {});
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	auto const result = f.cache.try_hash_piece(f.loc(0_piece), hash_job.get());
	t.join();

	// try_hash_piece parks the job in all three modes: v1/hybrid via the
	// hashing_flag branch (kick_hasher mid-run), v2-only via the v2_pending
	// branch (drain mid-run). The dispatched job lands in completed for
	// v1-only (kick_hasher's main path) and in retry for v2 and hybrid
	// (kick_hasher routes hybrid through retry; the v2-only drain posts
	// the hung job there too).
	TEST_EQUAL(result, disk_cache::hash_result::job_queued);
	auto& dispatched = bool(mode & test_mode::v2) ? retry : completed;
	TEST_EQUAL(dispatched.size(), 1);

	if (mode & test_mode::v1)
		TEST_CHECK(!std::get<job::hash>(hash_job->action).piece_hash.is_all_zeros());
	for (auto const& h : block_hashes)
		TEST_CHECK(!h.is_all_zeros());

	if (!(mode & test_mode::v1))
	{
		// v2-only: the dispatched job goes to retry; in production
		// pread_disk_io re-runs do_job(hash) which calls hash_piece,
		// whose scope_end sets piece_hash_returned_flag and lets pass-1
		// evict. The fixture doesn't drive that path, so the cpe stays
		// in the cache without the flag set -- skip the eviction check.
		return;
	}
	TEST_EQUAL(f.flush(), 1);
	TEST_EQUAL(int(f.cache.size()), 0);
}

// kick_hasher advances the cursor as far as it can (block 0 present) then
// stalls because block 1 is absent from the cache. A hash job that was hung
// on the piece while hashing_flag was set must come back in retry_jobs so
// pread_disk_io can re-dispatch it (e.g. to read the missing block from disk).
void test_hash_job_retry_when_piece_incomplete(test_mode_t const mode)
{
	// 2-block piece; insert only block 0. kick_hasher will hash block 0,
	// reach cursor=1, find block 1 absent, and stop without completing.
	cache_fixture f(2, mode);
	f.insert(0_piece, 0);

	std::vector<sha256_hash> block_hashes(bool(mode & test_mode::v2) ? 2 : 0);
	auto hash_job = std::make_unique<pread_disk_job>();
	hash_job->action = job::hash{
		{}, 0_piece,
		span<sha256_hash>{block_hashes.data(), int(block_hashes.size())},
		sha1_hash{}
	};

	jobqueue_t completed, retry;

	// Slow hashing gives us a wide window. Run the hasher and the v2 drain
	// on the same thread, park the hash job while hashing_flag is set, then
	// let the hasher finish.
	std::thread t([&]() {
		f.cache.kick_pending_hashers(completed, retry);
		f.cache.drain_v2_hash_queue(
			[&block_hashes](std::shared_ptr<lt::aux::pread_storage> const&,
				lt::piece_index_t,
				int const block,
				lt::sha256_hash const& h) {
				if (block < int(block_hashes.size())) block_hashes[std::size_t(block)] = h;
			},
			retry,
			[](jobqueue_t, disk_job*) {});
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// For v1/hybrid the hasher has set hashing_flag and block 1 is absent,
	// so it will stall at cursor=1; try_hash_piece parks the job
	// (job_queued) and the retry mechanism handles the case where the
	// hasher can't complete. For v2-only the drain is mid-run and
	// v2_pending > 0 parks the job through the same job_queued path.
	auto const result = f.cache.try_hash_piece(f.loc(0_piece), hash_job.get());
	// v1/hybrid: parked via hashing_flag (kick_hasher mid-run).
	// v2-only: parked via v2_pending (drain mid-run).
	TEST_EQUAL(result, disk_cache::hash_result::job_queued);
	t.join();

	// v1/hybrid: kick_hasher hashed block 0 (cursor=1) but stopped because
	// block 1 is absent, and posted the hung job to retry.
	// v2-only: drain processed block 0's queue entry, found the hung
	// hash_job, and posted it to retry. Either way the job lands in retry.
	TEST_EQUAL(completed.size(), 0);
	TEST_EQUAL(retry.size(), 1);
	TEST_CHECK(retry.pop_front() == hash_job.get());

	// Insert block 1, run the hasher to completion, then flush.
	f.insert(0_piece, 1);
	f.kick_hashers();
	TEST_EQUAL(f.flush(), 2);
	TEST_EQUAL(int(f.cache.size()), 0);
}

// Exercises phase 3 of flush_to_disk - the "drop held-alive" pass.
//
// After a block is written to disk we usually keep its buffer in the cache so
// the piece hash can be computed in-memory rather than read back from disk.
// kick_hasher naturally releases these buffers as it hashes past them, but if
// the hasher cursor can't advance (e.g. v1 SHA-1 is blocked on a missing
// earlier block), they sit pinned in the cache. Phase 3 evicts them anyway
// when the cache is full and we need the room - sacrificing the read-back
// optimization is cheaper than letting back-pressure stall the producer.
//
// Setup: a 3-block piece with blocks 0 and 2 inserted (block 1 missing).
// The hasher thread runs kick_pending_hashers; under slow-hash it spends
// ~1.6s in each hash update, holding hashing_flag with hasher_cursor=0.
// During that window the main thread calls flush_to_disk(target=0): phase 4
// flushes blocks 0 and 2 via flush_piece_impl, and because hashing_flag is
// set and both block indices are >= hasher_cursor, both transition to the
// held-alive state (disk_buffer_ref{buf}) - m_blocks does NOT drop.
// When kick_hasher finishes, cursor=1 (block 0 hashed contiguously, block 1
// missing stops it). Its cleanup pass over [0, 1) reclaims block 0. Block 2
// (index 2 >= cursor=1) is left held-alive.
// A subsequent flush_to_disk's phase 3 must free block 2's buffer with no
// disk I/O - the data has already been written.
void test_drop_held_alive_buffers(test_mode_t const mode)
{
	// The held-alive mechanism only applies to v1 pieces: kick_hasher pins
	// block buffers until the SHA-1 hasher reaches them. For pieces with
	// any v2 component (v2-only or hybrid) the insert path moves wjob.buf
	// into m_v2_hash_queue, so cbe never owns the buffer; the data is
	// owned by the queue entry and freed by drain_v2_hash_queue.
	if (mode & test_mode::v2) return;

	cache_fixture f(3, mode);
	f.insert(0_piece, 0);
	f.insert(0_piece, 2);
	TEST_EQUAL(int(f.cache.size()), 2);
	TEST_EQUAL(f.alloc.live, 2);

	jobqueue_t completed, retry;

	std::thread hasher_thread([&]() { f.cache.kick_pending_hashers(completed, retry); });
	// Wait until kick_hasher has released the mutex inside its slow hash
	// update. The 50ms is comfortably inside the ~1.6s slow-hash window.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// Flush while hashing_flag is set on the piece and hasher_cursor=0.
	// In phase 4 of flush_to_disk, flush_piece_impl flushes blocks 0 and 2;
	// the needed_by_hasher branch puts both into the held-alive state.
	TEST_EQUAL(f.flush(), 2);

	hasher_thread.join();

	// kick_hasher hashed block 0 contiguously (cursor advanced to 1), then
	// hit the gap at block 1 and stopped. Its cleanup pass over [0, 1)
	// released block 0's held-alive buffer. Block 2 remains held-alive at
	// index 2 >= cursor=1 - exactly the state phase 3 is meant to clean up.
	TEST_EQUAL(int(f.cache.size()), 1);
	TEST_EQUAL(f.alloc.live, 1);

	// Phase 3 of flush_to_disk reclaims block 2's held-alive buffer.
	// No write_jobs remain, so the flush callback returns 0.
	TEST_EQUAL(f.flush(), 0);
	TEST_EQUAL(int(f.cache.size()), 0);
	TEST_EQUAL(f.alloc.live, 0);

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_CHECK(aborted.empty());
}

// try_clear_piece() arrives while a hasher thread holds hashing_flag (mutex
// released mid-hash) -- the disk-write-failure path can race with kick_hasher
// on the same piece. try_clear_piece must park the clear on the cpe (not run
// clear_piece_impl while ph/hasher_cursor are in use). When kick_hasher
// finishes via Path 2 it sets force_flush_flag; the next flush sees the
// parked clear in flush_piece_impl's scope_end and dispatches it.
void test_clear_piece_during_hashing(test_mode_t const mode)
{
	if (!(mode & test_mode::v1)) return; // v2-only has no hashing_flag

	cache_fixture f(2, mode);
	f.insert(0_piece, 0);
	f.insert(0_piece, 1);

	auto clear_job = std::make_unique<pread_disk_job>();
	clear_job->action = job::clear_piece{{}, 0_piece};

	jobqueue_t completed, retry;
	std::thread hasher_thread([&]() { f.cache.kick_pending_hashers(completed, retry); });

	// Give the hasher time to set hashing_flag and release the mutex inside
	// its slow-hash update. The window is ~1.6s per block; 50ms is well inside.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	jobqueue_t cb_aborted;
	bool const cb_immediate = f.cache.try_clear_piece(f.loc(0_piece), clear_job.get(), cb_aborted);
	// hashing_flag was set -- try_clear_piece parked the clear and returned false.
	TEST_CHECK(!cb_immediate);
	TEST_CHECK(cb_aborted.empty());

	hasher_thread.join();
	// kick_hasher's Path 2 (all blocks hashed, no hash_job) cleared
	// hashing_flag and set force_flush_flag.

	// Run a flush. Pass 1 picks up the piece (force_flush_flag), flushes its
	// write_jobs, then flush_piece_impl's scope_end sees cpe.clear_piece set
	// and dispatches the parked clear via clear_piece_fun.
	jobqueue_t deferred_aborted;
	disk_job* deferred_clear = nullptr;
	f.cache.flush_to_disk(
		[&](bitfield& flushed, span<disk_job* const> blocks) -> int {
			int count = 0;
			for (int i = 0; i < int(blocks.size()); ++i)
			{
				if (!blocks[i]) continue;
				flushed.set_bit(i);
				++count;
			}
			return count;
		},
		0,
		[&](jobqueue_t aborted, disk_job* clear) {
			while (!aborted.empty())
				deferred_aborted.push_back(aborted.pop_front());
			if (clear) deferred_clear = clear;
		},
		false);

	// Both write_jobs were already taken by the flush, so the deferred clear's
	// aborted is empty. The clear job itself was dispatched.
	TEST_CHECK(deferred_aborted.empty());
	TEST_EQUAL(deferred_clear, clear_job.get());
	TEST_EQUAL(int(f.cache.size()), 0);
	TEST_EQUAL(f.alloc.live, 0);
}

// Regression test for the pending_free_flag mechanism.
//
// flush_storage() is called to tear down a storage while a hasher thread
// holds hashing_flag (mutex released mid-hash). flush_storage() must NOT
// free or erase the piece entry while the hasher is accessing ph and
// block_hashes; instead it sets pending_free_flag and defers the erasure.
// When kick_hasher() subsequently clears hashing_flag it sees
// pending_free_flag and erases the piece itself.
//
// hashing_flag is set by kick_pending_hashers, which only kicks v1/hybrid
// pieces. v2-only pieces have no hashing_flag (their work happens in
// drain_v2_hash_queue), so they don't exercise the pending_free_flag race.
void test_flush_storage_during_hashing(test_mode_t const mode)
{
	if (!(mode & test_mode::v1)) return;

	// 2-block piece so the hasher has real work to do.
	cache_fixture f(2, mode);
	f.insert(0_piece, 0);
	f.insert(0_piece, 1);

	jobqueue_t completed, retry;

	// With slow hashing each block takes ~1.6 s. Run the hasher in a
	// background thread so flush_storage() races with it.
	std::thread hasher_thread([&]() {
		f.cache.kick_pending_hashers(completed, retry);
	});

	// Give the hasher time to set hashing_flag and release the mutex.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// flush_storage() sees hashing_flag set: it flushes the blocks to disk,
	// sets pending_free_flag, and returns WITHOUT erasing the piece entry.
	f.flush_storage_for();

	// The hasher thread is still running; the piece entry must still exist.
	TEST_CHECK(f.cache.size() > 0);

	hasher_thread.join();
	// kick_hasher() saw pending_free_flag when clearing hashing_flag and
	// erased the piece itself.

	TEST_EQUAL(int(f.cache.size()), 0);
	TEST_EQUAL(f.alloc.live, 0);
}

}

TORRENT_TEST(test_pread_hash_job_retry)
{
	hash_job_retry_test(&lt::pread_disk_io_constructor, "test_pread_hash_retry");
}

TORRENT_TEST(test_posix_hash_job_retry)
{
	hash_job_retry_test(&lt::posix_disk_io_constructor, "test_posix_hash_retry");
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(test_mmap_hash_job_retry)
{
	hash_job_retry_test(&lt::mmap_disk_io_constructor, "test_mmap_hash_retry");
}
#endif

TORRENT_TEST(hash_job_dispatched_v1) { test_hash_job_dispatched_by_hasher(test_mode::v1); }
TORRENT_TEST(hash_job_dispatched_v2) { test_hash_job_dispatched_by_hasher(test_mode::v2); }
TORRENT_TEST(hash_job_dispatched_hybrid) { test_hash_job_dispatched_by_hasher(test_mode::v1 | test_mode::v2); }

TORRENT_TEST(hash_job_retry_incomplete_v1) { test_hash_job_retry_when_piece_incomplete(test_mode::v1); }
TORRENT_TEST(hash_job_retry_incomplete_v2) { test_hash_job_retry_when_piece_incomplete(test_mode::v2); }
TORRENT_TEST(hash_job_retry_incomplete_hybrid) { test_hash_job_retry_when_piece_incomplete(test_mode::v1 | test_mode::v2); }

TORRENT_TEST(flush_storage_during_hashing_v1)
	{ test_flush_storage_during_hashing(test_mode::v1); }
TORRENT_TEST(flush_storage_during_hashing_v2)
	{ test_flush_storage_during_hashing(test_mode::v2); }
TORRENT_TEST(flush_storage_during_hashing_hybrid)
	{ test_flush_storage_during_hashing(test_mode::v1 | test_mode::v2); }

	TORRENT_TEST(clear_piece_during_hashing_v1) { test_clear_piece_during_hashing(test_mode::v1); }
	TORRENT_TEST(clear_piece_during_hashing_hybrid)
	{
		test_clear_piece_during_hashing(test_mode::v1 | test_mode::v2);
	}

	TORRENT_TEST(drop_held_alive_v1) { test_drop_held_alive_buffers(test_mode::v1); }
	TORRENT_TEST(drop_held_alive_v2) { test_drop_held_alive_buffers(test_mode::v2); }
	TORRENT_TEST(drop_held_alive_hybrid)
	{
		test_drop_held_alive_buffers(test_mode::v1 | test_mode::v2);
	}

#endif // TORRENT_SIMULATE_SLOW_HASH
