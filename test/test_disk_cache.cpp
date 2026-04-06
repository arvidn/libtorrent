/*

Copyright (c) 2024-2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/visit_block_iovecs.hpp"
#include "libtorrent/hasher.hpp"
#include <array>
#include "test.hpp"
#include "test_utils.hpp"
#include "disk_cache_test_utils.hpp"

using lt::span;
using namespace lt;
using namespace lt::aux;

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
				auto const* wj = blocks[i].get_write_job();
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

namespace {

// Tests a piece where piece_size2 < piece_size: a v2 file boundary falls
// inside what would be a full-sized v1 piece, truncating the v2 view.
//
// v2-only: the piece has 1 block of 11 bytes (piece_size == piece_size2 == 11).
// hybrid:  the piece has 2 full v1 blocks but piece_size2 == 11, so the v2
//          hasher only covers 11 bytes even though 2 * default_block_size bytes
//          are written.
void test_piece_size2_smaller_than_piece_size(test_mode_t const mode)
{
	constexpr int v1_piece_size = 2 * default_block_size;
	constexpr int piece_size2   = 11;

	bool const need_v1 = bool(mode & test_mode::v1);
	bool const need_v2 = bool(mode & test_mode::v2);

	// The effective piece size governs block layout: v1 size when v1 is
	// active, v2 size (piece_size2) for v2-only pieces.
	int const effective_piece_size = need_v1 ? v1_piece_size : piece_size2;
	int const blocks_in_piece = (effective_piece_size + default_block_size - 1)
		/ default_block_size;

	cache_fixture f(blocks_in_piece, mode);

	disk_cache::piece_entry_params const params{
		piece_size2,
		effective_piece_size,
		need_v1,
		need_v2
	};

	for (int blk = 0; blk < blocks_in_piece; ++blk)
	{
		int const buf_size = std::min(default_block_size,
			effective_piece_size - blk * default_block_size);
		f.insert(0_piece, blk, params, buf_size);
	}

	jobqueue_t completed, retry;
	f.cache.kick_pending_hashers(completed, retry);

	int const v2_blocks = (piece_size2 + default_block_size - 1) / default_block_size;
	std::vector<sha256_hash> block_hashes(need_v2 ? v2_blocks : 0);
	auto hash_job = std::make_unique<pread_disk_job>();
	hash_job->action = job::hash{
		{}, 0_piece,
		span<sha256_hash>{block_hashes.data(), int(block_hashes.size())},
		sha1_hash{}
	};
	TEST_EQUAL(f.cache.try_hash_piece(f.loc(0_piece), hash_job.get())
		, disk_cache::hash_result::job_completed);
	if (need_v1)
		TEST_CHECK(!std::get<job::hash>(hash_job->action).piece_hash.is_all_zeros());
	for (auto const& h : block_hashes)
		TEST_CHECK(!h.is_all_zeros());

	TEST_EQUAL(f.flush(), blocks_in_piece);
	TEST_EQUAL(int(f.cache.size()), 0);
}

}

TORRENT_TEST(truncated_v2_piece_v2)
	{ test_piece_size2_smaller_than_piece_size(test_mode::v2); }
TORRENT_TEST(truncated_v2_piece_hybrid)
	{ test_piece_size2_smaller_than_piece_size(test_mode::v1 | test_mode::v2); }

