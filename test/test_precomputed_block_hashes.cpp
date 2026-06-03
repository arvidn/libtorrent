/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "test_utils.hpp" // for the _piece literal
#include "libtorrent/aux_/precomputed_block_hashes.hpp"
#include "libtorrent/hasher.hpp"

#include <string>

using lt::aux::precomputed_block_hashes;

namespace {

	// a distinct, non-zero SHA-256 for a given block id
	lt::sha256_hash test_hash(int const v)
	{
		std::string const s = "block-" + std::to_string(v);
		lt::hasher256 h;
		h.update({s.data(), int(s.size())});
		return h.final();
	}

} // anonymous namespace

// store some blocks of a piece, then take the whole piece. Stored blocks come
// back at their indices; un-stored blocks read as all-zero.
TORRENT_TEST(precomputed_block_hashes_store_take)
{
	precomputed_block_hashes cache;
	cache.store(3_piece, 0, 4, test_hash(0));
	cache.store(3_piece, 2, 4, test_hash(2));

	lt::aux::vector<lt::sha256_hash> const v = cache.take(3_piece);
	TEST_EQUAL(int(v.size()), 4);
	TEST_CHECK(v[0] == test_hash(0));
	TEST_CHECK(v[1].is_all_zeros()); // never stored -> "missing"
	TEST_CHECK(v[2] == test_hash(2));
	TEST_CHECK(v[3].is_all_zeros());

	// taking removes the piece
	TEST_CHECK(cache.take(3_piece).empty());
}

// taking a piece that was never stored yields an empty result / nullopt
TORRENT_TEST(precomputed_block_hashes_missing_piece)
{
	precomputed_block_hashes cache;
	TEST_CHECK(cache.take(0_piece).empty());
	TEST_CHECK(!cache.take_block(0_piece, 0).has_value());
}

// take_block returns a single stored hash and consumes it; un-stored blocks
// are reported missing.
TORRENT_TEST(precomputed_block_hashes_take_block)
{
	precomputed_block_hashes cache;
	cache.store(1_piece, 1, 3, test_hash(1));

	TEST_CHECK(!cache.take_block(1_piece, 0).has_value()); // not stored
	TEST_CHECK(!cache.take_block(1_piece, 2).has_value());

	auto const h = cache.take_block(1_piece, 1);
	TEST_CHECK(h.has_value());
	TEST_CHECK(*h == test_hash(1));

	// consumed: missing now, and the piece (no hashes left) is gone
	TEST_CHECK(!cache.take_block(1_piece, 1).has_value());
	TEST_CHECK(cache.take(1_piece).empty());
}

// a block consumed via take_block is no longer present in a later take()
TORRENT_TEST(precomputed_block_hashes_take_block_then_take)
{
	precomputed_block_hashes cache;
	cache.store(2_piece, 0, 3, test_hash(0));
	cache.store(2_piece, 1, 3, test_hash(1));
	cache.store(2_piece, 2, 3, test_hash(2));

	auto const h1 = cache.take_block(2_piece, 1);
	TEST_CHECK(h1.has_value() && *h1 == test_hash(1));

	lt::aux::vector<lt::sha256_hash> const v = cache.take(2_piece);
	TEST_EQUAL(int(v.size()), 3);
	TEST_CHECK(v[0] == test_hash(0));
	TEST_CHECK(v[1].is_all_zeros()); // already consumed
	TEST_CHECK(v[2] == test_hash(2));
}

// consuming every block via take_block drops the piece
TORRENT_TEST(precomputed_block_hashes_take_block_drains_piece)
{
	precomputed_block_hashes cache;
	cache.store(4_piece, 0, 2, test_hash(0));
	cache.store(4_piece, 1, 2, test_hash(1));
	TEST_CHECK(cache.take_block(4_piece, 0).has_value());
	TEST_CHECK(cache.take_block(4_piece, 1).has_value());
	TEST_CHECK(cache.take(4_piece).empty());
}

// drop() discards a piece's hashes
TORRENT_TEST(precomputed_block_hashes_drop)
{
	precomputed_block_hashes cache;
	cache.store(5_piece, 0, 2, test_hash(0));
	cache.drop(5_piece);
	TEST_CHECK(cache.take(5_piece).empty());
	TEST_CHECK(!cache.take_block(5_piece, 0).has_value());
}

// storing the same block twice overwrites the hash
TORRENT_TEST(precomputed_block_hashes_overwrite)
{
	precomputed_block_hashes cache;
	cache.store(0_piece, 0, 1, test_hash(0));
	cache.store(0_piece, 0, 1, test_hash(9));
	lt::aux::vector<lt::sha256_hash> const v = cache.take(0_piece);
	TEST_EQUAL(int(v.size()), 1);
	TEST_CHECK(v[0] == test_hash(9));
}

// take_block out of range is reported missing, not a crash
TORRENT_TEST(precomputed_block_hashes_take_block_out_of_range)
{
	precomputed_block_hashes cache;
	cache.store(7_piece, 0, 2, test_hash(0));
	TEST_CHECK(!cache.take_block(7_piece, 5).has_value());
}
