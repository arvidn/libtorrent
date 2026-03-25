/*

Copyright (c) 2024-2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/visit_block_iovecs.hpp"
#include "libtorrent/aux_/disk_cache.hpp"
#include "libtorrent/aux_/pread_disk_job.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp" // jobqueue_t
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/disk_interface.hpp"  // default_block_size
#include "libtorrent/io_context.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/units.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
#include "test.hpp"
#include "test_utils.hpp"

using lt::span;
using namespace lt;
using namespace lt::aux;

using test_mode_t = lt::flags::bitfield_flag<std::uint8_t, struct test_mode_tag>;
namespace test_mode {
	using lt::operator ""_bit;
	constexpr test_mode_t v1 = 0_bit;
	constexpr test_mode_t v2 = 1_bit;
}

namespace {

struct tbe
{
	span<char const> write_buf() const
	{
		return _buf;
	}
	span<char const> _buf;
};

template <size_t N>
tbe b(char const (&literal)[N])
{
	auto buf = span<char const>{&literal[0], N - 1};
	return tbe{buf};
}

std::string join(span<span<char const>> iovec)
{
	std::string ret;
	for (span<char const> const& b : iovec)
	{
		ret.append(b.begin(), b.end());
	}
	return ret;
}

struct test_allocator : buffer_allocator_interface
{
	void free_disk_buffer(char* b) override { delete[] b; --live; }
	void free_multiple_buffers(span<char*> bufs) override
	{
		for (char* b : bufs) free_disk_buffer(b);
	}

	disk_buffer_holder alloc(int const size = default_block_size)
	{
		++live;
		return disk_buffer_holder(*this, new char[static_cast<size_t>(size)], size);
	}

	virtual ~test_allocator() = default;

	int live = 0;
};

// drives disk_cache directly without any storage layer.
// The piece metadata (blocks_per_piece, v1, v2) is specified in the
// constructor and passed straight to disk_cache::insert() via
// piece_entry_params - no file_storage or pread_storage involved.
struct cache_fixture
{
	io_context ios;
	disk_cache cache{ios};
	test_allocator alloc;
	std::vector<std::unique_ptr<pread_disk_job>> live_jobs;

	// All pieces created by this fixture share the same shape.
	int const blocks_per_piece;
	test_mode_t const mode;

	cache_fixture(int const blocks_per_piece_, test_mode_t const mode_)
		: blocks_per_piece(blocks_per_piece_)
		, mode(mode_)
	{
		TORRENT_ASSERT(mode & (test_mode::v1 | test_mode::v2));
		cache.set_max_size(1024); // generous; no back-pressure by default
	}

	piece_location loc(piece_index_t const p) const
	{
		return {storage_index_t{0}, p};
	}

	disk_cache::piece_entry_params piece_params() const
	{
		return {
			blocks_per_piece,
			blocks_per_piece * default_block_size, // piece_size2
			bool(mode & test_mode::v1), bool(mode & test_mode::v2)
		};
	}

	// Allocate a write-job whose buffer is filled with fill_char.
	// Ownership is transferred to live_jobs; the raw pointer is returned.
	// write_job->storage is intentionally null: disk_cache no longer needs it.
	pread_disk_job* make_write_job(
		piece_index_t const piece
		, int const block
		, char const fill = 0x5a)
	{
		auto j = std::make_unique<pread_disk_job>();
		auto buf = alloc.alloc();
		std::memset(buf.data(), fill, std::size_t(buf.size()));
		j->action = job::write{
			{},
			std::move(buf),
			piece,
			block * default_block_size,
			static_cast<std::uint16_t>(default_block_size)
		};
		auto* ret = j.get();
		live_jobs.push_back(std::move(j));
		return ret;
	}

	insert_result_flags insert(
		piece_index_t const piece
		, int const block
		, bool const force_flush = false
		, char const fill = 0x5a)
	{
		return cache.insert(loc(piece), block, force_flush, nullptr
			, make_write_job(piece, block, fill)
			, piece_params());
	}

	// Simulate flushing: marks every block that has a write_job as flushed.
	// Returns the number of blocks flushed.
	int flush(int const target = 0, bool const optimistic = false)
	{
		int total = 0;
		cache.flush_to_disk(
			[&](bitfield& flushed, span<cached_block_entry const> blocks) -> int {
				int count = 0;
				for (int i = 0; i < int(blocks.size()); ++i)
				{
					if (!blocks[i].write_job) continue;
					flushed.set_bit(i);
					++count;
				}
				total += count;
				return count;
			},
			target,
			[](jobqueue_t, disk_job*) {}
			, optimistic);
		return total;
	}

	// Advance the hasher for all pending pieces, discarding completed jobs.
	void kick_hashers()
	{
		jobqueue_t completed, retry;
		cache.kick_pending_hashers(completed, retry);
	}

};

}

TORRENT_TEST(visit_block_iovecs_full)
{
	std::array<tbe, 5> const blocks{b("a"), b("b"), b("c"), b("d"), b("e")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		TEST_EQUAL(cnt, 0);
		TEST_EQUAL(start_idx, 0);
		TEST_EQUAL(iovec.size(), 5);
		TEST_EQUAL(join(iovec), "abcde");
		++cnt;
		return false;
	});
}

TORRENT_TEST(visit_block_iovecs_one_hole)
{
	std::array<tbe, 5> const blocks{b("a"), b("b"), b(""), b("d"), b("e")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		switch (cnt) {
			case 0:
				TEST_EQUAL(start_idx, 0);
				TEST_EQUAL(iovec.size(), 2);
				TEST_EQUAL(join(iovec), "ab");
				break;
			case 1:
				TEST_EQUAL(start_idx, 3);
				TEST_EQUAL(iovec.size(), 2);
				TEST_EQUAL(join(iovec), "de");
				break;
			default:
				TORRENT_ASSERT_FAIL();
		}
		++cnt;
		return false;
	});
}

TORRENT_TEST(visit_block_iovecs_two_holes)
{
	std::array<tbe, 5> const blocks{b("a"), b(""), b("c"), b(""), b("e")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		switch (cnt) {
			case 0:
				TEST_EQUAL(start_idx, 0);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "a");
				break;
			case 1:
				TEST_EQUAL(start_idx, 2);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "c");
				break;
			case 2:
				TEST_EQUAL(start_idx, 4);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "e");
				break;
			default:
				TORRENT_ASSERT_FAIL();
		}
		++cnt;
		return false;
	});
}


TORRENT_TEST(visit_block_iovecs_interrupt)
{
	std::array<tbe, 3> const blocks{b("a"), b(""), b("c")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		switch (cnt) {
			case 0:
				TEST_EQUAL(start_idx, 0);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "a");
				break;
			default:
				TORRENT_ASSERT_FAIL();
		}
		++cnt;
		return true;
	});
}

TORRENT_TEST(visit_block_iovecs_leading_hole)
{
	std::array<tbe, 5> const blocks{b(""), b("a"), b("b"), b("c"), b("d")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		TEST_EQUAL(cnt, 0);
		TEST_EQUAL(start_idx, 1);
		TEST_EQUAL(iovec.size(), 4);
		TEST_EQUAL(join(iovec), "abcd");
		++cnt;
		return false;
	});
}

TORRENT_TEST(visit_block_iovecs_trailing_hole)
{
	std::array<tbe, 5> const blocks{b("a"), b("b"), b("c"), b("d"), b("")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		TEST_EQUAL(cnt, 0);
		TEST_EQUAL(start_idx, 0);
		TEST_EQUAL(iovec.size(), 4);
		TEST_EQUAL(join(iovec), "abcd");
		++cnt;
		return false;
	});
}

namespace {

// all blocks arrive and are hashed before flush
void test_disk_bottleneck(test_mode_t const mode)
{
	cache_fixture f(2, mode);

	auto r0 = f.insert(0_piece, 0);
	TEST_CHECK(bool(r0 & disk_cache::need_hasher_kick));

	auto r1 = f.insert(0_piece, 1);
	// needs_hasher_kick_flag already set from block 0 — not raised again.
	TEST_CHECK(!(r1 & disk_cache::need_hasher_kick));

	TEST_EQUAL(int(f.cache.size()), 2);

	jobqueue_t completed, retry;
	TEST_CHECK(f.cache.kick_pending_hashers(completed, retry));
	TEST_CHECK(completed.empty());

	std::vector<sha256_hash> block_hashes(mode & test_mode::v2 ? 2 : 0);
	auto hash_job = std::make_unique<pread_disk_job>();
	hash_job->action = job::hash{
		{}, 0_piece,
		span<sha256_hash>{block_hashes.data(), int(block_hashes.size())},
		sha1_hash{}
	};

	TEST_EQUAL(f.cache.try_hash_piece(f.loc(0_piece), hash_job.get())
		, disk_cache::hash_result::job_completed);
	if (mode & test_mode::v1)
		TEST_CHECK(!std::get<job::hash>(hash_job->action).piece_hash.is_all_zeros());
	for (auto const& h : block_hashes)
		TEST_CHECK(!h.is_all_zeros());

	TEST_EQUAL(f.flush(), 2);
	TEST_EQUAL(int(f.cache.size()), 0);
}

// block 0 flushed before the hasher runs — try_hash_piece returns post_job
void test_hashing_bottleneck(test_mode_t const mode)
{
	cache_fixture f(2, mode);

	f.insert(0_piece, 0);
	TEST_EQUAL(f.flush(0), 1);
	f.insert(0_piece, 1);

	std::vector<sha256_hash> block_hashes(mode & test_mode::v2 ? 2 : 0);
	auto hash_job = std::make_unique<pread_disk_job>();
	hash_job->action = job::hash{
		{}, 0_piece,
		span<sha256_hash>{block_hashes.data(), int(block_hashes.size())},
		sha1_hash{}
	};

	TEST_EQUAL(f.cache.try_hash_piece(f.loc(0_piece), hash_job.get())
		, disk_cache::hash_result::post_job);

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_EQUAL(aborted.size(), 1);
	TEST_EQUAL(int(f.cache.size()), 0);
}

// three independent pieces: each hashed and flushed independently
void test_multi_piece(test_mode_t const mode)
{
	cache_fixture f(1, mode);

	f.insert(0_piece, 0);
	f.insert(1_piece, 0);
	f.insert(2_piece, 0);
	TEST_EQUAL(int(f.cache.size()), 3);

	jobqueue_t completed, retry;
	f.cache.kick_pending_hashers(completed, retry);

	for (auto p : {0_piece, 1_piece, 2_piece})
	{
		sha256_hash bh;
		auto hash_job = std::make_unique<pread_disk_job>();
		hash_job->action = job::hash{
			{}, p,
			(mode & test_mode::v2) ? span<sha256_hash>{&bh, 1} : span<sha256_hash>{},
			sha1_hash{}
		};
		TEST_EQUAL(f.cache.try_hash_piece(f.loc(p), hash_job.get())
			, disk_cache::hash_result::job_completed);
		if (mode & test_mode::v1)
			TEST_CHECK(!std::get<job::hash>(hash_job->action).piece_hash.is_all_zeros());
		if (mode & test_mode::v2)
			TEST_CHECK(!bh.is_all_zeros());
	}

	TEST_EQUAL(f.flush(), 3);
	TEST_EQUAL(int(f.cache.size()), 0);
}

// try_hash_piece is called while kick_pending_hashers is mid-run:
// the hash job is hung on the piece (job_queued) and dispatched by the hasher
// once it completes. This exercises the concurrent path in kick_hasher where
// hash_job is non-null when hashing finishes.
//
// Without TORRENT_SIMULATE_SLOW_HASH the hashing window is too narrow to
// catch reliably; the test falls back to the sequential job_completed path.
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

#ifdef TORRENT_SIMULATE_SLOW_HASH
	// With slow hashing each block takes ~1.6 s. Run the hasher in a thread
	// and call try_hash_piece after a short sleep, reliably catching the
	// window where hashing_flag is set.
	std::thread t([&]() {
		f.cache.kick_pending_hashers(completed, retry);
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	auto const result = f.cache.try_hash_piece(f.loc(0_piece), hash_job.get());
	t.join();

	TEST_EQUAL(result, disk_cache::hash_result::job_queued);
	// The hasher filled in the hash values and posted the job.
	TEST_EQUAL(completed.size(), 1);
#else
	// Without slow hashing the window is negligible. Run them sequentially
	// to exercise hash value correctness via the job_completed path.
	f.cache.kick_pending_hashers(completed, retry);
	auto const result = f.cache.try_hash_piece(f.loc(0_piece), hash_job.get());
	TEST_EQUAL(result, disk_cache::hash_result::job_completed);
#endif

	if (mode & test_mode::v1)
		TEST_CHECK(!std::get<job::hash>(hash_job->action).piece_hash.is_all_zeros());
	for (auto const& h : block_hashes)
		TEST_CHECK(!h.is_all_zeros());

	TEST_EQUAL(f.flush(), 1);
	TEST_EQUAL(int(f.cache.size()), 0);
}

}

TORRENT_TEST(disk_bottleneck_v1) { test_disk_bottleneck(test_mode::v1); }
TORRENT_TEST(disk_bottleneck_v2) { test_disk_bottleneck(test_mode::v2); }
TORRENT_TEST(disk_bottleneck_hybrid) { test_disk_bottleneck(test_mode::v1 | test_mode::v2); }

TORRENT_TEST(hashing_bottleneck_v1) { test_hashing_bottleneck(test_mode::v1); }
TORRENT_TEST(hashing_bottleneck_v2) { test_hashing_bottleneck(test_mode::v2); }
TORRENT_TEST(hashing_bottleneck_hybrid) { test_hashing_bottleneck(test_mode::v1 | test_mode::v2); }

TORRENT_TEST(multi_piece_v1) { test_multi_piece(test_mode::v1); }
TORRENT_TEST(multi_piece_v2) { test_multi_piece(test_mode::v2); }
TORRENT_TEST(multi_piece_hybrid) { test_multi_piece(test_mode::v1 | test_mode::v2); }

TORRENT_TEST(hash_job_dispatched_v1) { test_hash_job_dispatched_by_hasher(test_mode::v1); }
TORRENT_TEST(hash_job_dispatched_v2) { test_hash_job_dispatched_by_hasher(test_mode::v2); }
TORRENT_TEST(hash_job_dispatched_hybrid) { test_hash_job_dispatched_by_hasher(test_mode::v1 | test_mode::v2); }

// v2, hashing is the bottleneck: block 0 flushed before SHA256 is computed.
// hash2() must invoke the fallback (read from disk).
TORRENT_TEST(v2_hashing_bottleneck)
{
	// 1-block piece for simplicity.
	cache_fixture f(1, test_mode::v2);

	f.insert(0_piece, 0);
	// Flush before the hasher runs — block_hashes[0] stays all-zeros.
	TEST_EQUAL(f.flush(0), 1);

	// The precomputed hash is absent and the buffer is gone; fallback fires.
	bool fallback_called = false;
	sha256_hash h = f.cache.hash2(f.loc(0_piece), 0, [&]() -> sha256_hash {
		fallback_called = true;
		return sha256_hash{};
	});
	TEST_CHECK(fallback_called);
	(void)h;

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_CHECK(aborted.empty()); // block 0's write_job was consumed by the flush
	TEST_EQUAL(int(f.cache.size()), 0);
	TEST_EQUAL(f.alloc.live, 0); // buffer was freed when the block was flushed
}

// v2: hash2() served from precomputed in-cache hash (no fallback needed).
TORRENT_TEST(v2_hash2_from_cache)
{
	cache_fixture f(1, test_mode::v2);

	f.insert(0_piece, 0);

	// Let the hasher compute SHA256 for block 0.
	jobqueue_t completed, retry;
	f.cache.kick_pending_hashers(completed, retry);

	bool fallback_called = false;
	sha256_hash h = f.cache.hash2(f.loc(0_piece), 0, [&]() -> sha256_hash {
		fallback_called = true;
		return sha256_hash{};
	});
	TEST_CHECK(!fallback_called);
	TEST_CHECK(!h.is_all_zeros());

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_EQUAL(aborted.size(), 1); // block 0's write_job was never flushed
	TEST_EQUAL(int(f.cache.size()), 0);
}

// Clear a piece that was never flushed or hashed.
// All write_jobs must be returned in the aborted queue.
TORRENT_TEST(clear_piece_v1)
{
	cache_fixture f(2, test_mode::v1);

	f.insert(0_piece, 0);
	f.insert(0_piece, 1);
	TEST_EQUAL(int(f.cache.size()), 2);

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_EQUAL(int(f.cache.size()), 0);
	TEST_EQUAL(aborted.size(), 2);
}

// Clear a piece that is not in the cache — returns true immediately.
TORRENT_TEST(clear_piece_not_in_cache)
{
	cache_fixture f(1, test_mode::v1);

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_CHECK(aborted.empty());
	TEST_EQUAL(int(f.cache.size()), 0);
}

// Hash failure: piece is hashed in-cache, hash check fails, piece is cleared
// before flushing. Both write_jobs must appear in the aborted queue.
TORRENT_TEST(hash_failure_clear)
{
	cache_fixture f(2, test_mode::v1);

	f.insert(0_piece, 0);
	f.insert(0_piece, 1);

	jobqueue_t completed, retry;
	f.cache.kick_pending_hashers(completed, retry);

	auto hash_job = std::make_unique<pread_disk_job>();
	hash_job->action = job::hash{{}, 0_piece, span<sha256_hash>{}, sha1_hash{}};
	TEST_EQUAL(f.cache.try_hash_piece(f.loc(0_piece), hash_job.get())
		, disk_cache::hash_result::job_completed);

	// Simulate hash mismatch: clear the piece before it reaches disk.
	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_EQUAL(aborted.size(), 2);
	TEST_EQUAL(int(f.cache.size()), 0);
}

// Clear a partially-flushed piece.
// Block 0 is hashed then flushed (its write_job is consumed by the flush).
// Block 1 is only in cache. Only block 1's write_job is aborted.
TORRENT_TEST(clear_piece_partially_flushed)
{
	cache_fixture f(2, test_mode::v1);

	// Insert and hash block 0 so hasher_cursor advances to 1.
	f.insert(0_piece, 0);
	{
		jobqueue_t completed, retry;
		f.cache.kick_pending_hashers(completed, retry);
	}

	// Flush block 0 (cheap: already hashed).
	f.flush(0);

	// Insert block 1.
	f.insert(0_piece, 1);
	TEST_EQUAL(int(f.cache.size()), 1); // only block 1's buffer remains

	jobqueue_t aborted;
	TEST_CHECK(f.cache.try_clear_piece(f.loc(0_piece), nullptr, aborted));
	TEST_EQUAL(int(f.cache.size()), 0);
	TEST_EQUAL(aborted.size(), 1); // only block 1's write_job
}

namespace {

struct flush_test_case
{
	// {initial, expected} per piece
	aux::vector<std::pair<std::string, std::string>, piece_index_t> pieces;
	int target;    // target block count left in cache after flush
	bool optimistic; // optimistic flush
};

void run_flush_test(flush_test_case tc)
{
	TORRENT_ASSERT(!tc.pieces.empty());
	piece_index_t const num_pieces = tc.pieces.end_index();

	// Erase '|' from each piece's initial state, remembering its position.
	// All pieces must have equal block count.
	aux::vector<int, piece_index_t> cursor(num_pieces);

	int n = -1;
	for (piece_index_t const p : tc.pieces.range())
	{
		auto& s = tc.pieces[p].first;
		auto const bar = s.find('|');
		TORRENT_ASSERT(bar != std::string::npos);
		cursor[p] = int(bar);
		s.erase(bar, 1);
		TORRENT_ASSERT(n == -1 || n == int(s.size()));
		n = int(s.size());
	}

	cache_fixture f(n, test_mode::v1);

	// Insert blocks before the cursor (hashed region) before kicking.
	// '.' and '!' both represent write_jobs in the cache; '!' means the
	// callback will refuse to flush that block (simulating a partial flush).
	for (piece_index_t const p : tc.pieces.range())
		for (int i = 0; i < cursor[p]; ++i)
		{
			char const c = tc.pieces[p].first[size_t(i)];
			if (c != '.' && c != '!') continue;
			f.insert(p, i);
		}

	// Kick the hasher, advancing all pieces to their cursor.
	f.kick_hashers();

	// Insert blocks at or after the cursor (unhashed) after the kick.
	for (piece_index_t const p : tc.pieces.range())
		for (int i = cursor[p]; i < n; ++i)
		{
			char const c = tc.pieces[p].first[size_t(i)];
			if (c != '.' && c != '!') continue;
			f.insert(p, i);
		}

	// Run the flush. The callback stamps '#' on flushed blocks directly into
	// the initial string. '!' blocks are present in the cache but skipped,
	// simulating a callback that only partially flushes a piece.
	f.cache.flush_to_disk(
		[&](bitfield& flushed, span<cached_block_entry const> blocks) -> int
		{
			int count = 0;
			for (int i = 0; i < blocks.size(); ++i)
			{
				auto const* wj = blocks[i].write_job;
				if (!wj) continue;
				auto const& w = std::get<job::write>(wj->action);
				auto const blk = static_cast<std::size_t>(w.offset / default_block_size);
				if (tc.pieces[w.piece].first[blk] == '!') continue;
				tc.pieces[w.piece].first[blk] = '#';
				flushed.set_bit(i);
				++count;
			}
			return count;
		},
		tc.target,
		[](jobqueue_t, disk_job*) {},
		tc.optimistic);

	for (piece_index_t const p : tc.pieces.range())
	{
		// '!' blocks were skipped by the callback; they remain in the cache
		// as ordinary unflushed blocks, indistinguishable from '.' in the result.
		for (char& c : tc.pieces[p].first)
			if (c == '!') c = '.';
		tc.pieces[p].first.insert(size_t(cursor[p]), 1, '|');
		TEST_EQUAL(tc.pieces[p].first, tc.pieces[p].second);
	}

	// Verify cache size and live buffer count: only '.' blocks (unflushed
	// write_jobs) hold a buffer. Flushed ('#') blocks have had their buffer
	// freed via free_disk_buffer(); the live count confirms this.
	int expected_cache_size = 0;
	for (auto const& [ini, exp] : tc.pieces)
		for (char c : exp)
			if (c == '.') ++expected_cache_size;
	TEST_EQUAL(int(f.cache.size()), expected_cache_size);
	TEST_EQUAL(f.alloc.live, expected_cache_size);
}

}

// State string encoding — one character per block, with a cursor marker:
//
//   '|'  marks the hasher cursor. Blocks to its left have been hashed;
//        blocks to its right have not.
//   '.'  block in cache
//   '!'  block in cache; flush callback will refuse to flush it (initial only)
//   '#'  flushed to disk (expected strings only)
//   ' '  block not in cache
flush_test_case const flush_cases[] = {

	// Optimistic flush only flushes blocks that are fully downloaded and fully
	// hashed, never causing disk read-backs or partial writes.
	{{
		{"...|", "###|"},
		{"..|.", "..|."}
	 }, 0, true},

	{{
		{"|.. ", "|.. "},
		{"..| ", "..| "},
		{"| ..", "| .."},
		{"...|", "###|"},
		{"|   ", "|   "},
		{"  |.", "  |."}
	 }, 0, true},

	// non-optimistic flush will force flush pieces even that haven not been
	// hashed yet, but prefer the ones that have been hashed first. pieces with
	// more cheap flushable blocks are flushed first. To avoid small writes, if
	// we start flushing a piece, flush as many hashed blocks as possible.
	{{
		{"..|..", "..|.."},
		{"...|.", "###|."}
	}, 6, false},
	{{
		{"..|..", "..|.."},
		{"...|.", "###|."}
	}, 5, false},
	{{
		{"..|..", "##|.."},
		{"...|.", "###|."}
	}, 4, false},
	{{
		{"..|..", "##|.."},
		{"...|.", "###|."}
	}, 3, false},
	{{
		{"..|..", "##|##"},
		{"...|.", "###|."}
	 }, 2, false},
	{{
		{"..|..", "##|##"},
		{"...|.", "###|."}
	 }, 1, false},
	{{
		{"..|..", "##|##"},
		{"...|.", "###|#"}
	 }, 0, false},

	// flushed_cursor only advances through leading contiguous flushed blocks.

	{{
		{".!.|", "#.#|"}
	 }, 0, false},

	// target=2 prevents phase 3 from flushing the remaining blocks.
	{{
		{".!.|.", "#.#|."}
	 }, 2, false},

	{{
		{"|.!.", "|#.#"}
	 }, 0, false},
};

TORRENT_TEST(flush_ordering)
{
	for (auto const& tc : flush_cases)
		run_flush_test(tc);
}
