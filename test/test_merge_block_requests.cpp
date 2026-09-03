/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "test_utils.hpp" // for the _piece literal
#include "libtorrent/aux_/merge_block_requests.hpp"

using lt::piece_block;
using lt::aux::request_merger;

namespace {
constexpr int block_size = 0x4000; // 16 KiB
} // anonymous namespace

// two blocks in the same, full piece merge, contributing a full block's
// worth of bytes
TORRENT_TEST(merge_same_piece)
{
	request_merger merger(piece_block(0_piece, 0), 4 * block_size, 4 * block_size, block_size);
	int const bs = merger.merge(piece_block(0_piece, 1), 4 * block_size);
	TEST_EQUAL(bs, block_size);
	TEST_EQUAL(merger.piece_size(), 4 * block_size);
}

// a full piece is followed by the first block of the next (also full)
// piece: still contiguous, still merges. This is the common, non-v2-only
// case, large-block requests routinely span multiple whole pieces.
TORRENT_TEST(merge_across_full_piece_boundary)
{
	// last block of piece 0 (4 blocks/piece)
	request_merger merger(piece_block(0_piece, 3), 4 * block_size, 4 * block_size, block_size);
	int const bs = merger.merge(piece_block(1_piece, 0), 4 * block_size);
	TEST_EQUAL(bs, block_size);
}

// a run spanning several pieces merges block by block, with the merger
// tracking the current end of the run across calls
TORRENT_TEST(merge_multi_block_run)
{
	request_merger merger(piece_block(0_piece, 0), 4 * block_size, 4 * block_size, block_size);
	int total = block_size; // the first block, already merged in
	for (int i = 1; i < 4; ++i)
	{
		int const bs = merger.merge(piece_block(0_piece, i), 4 * block_size);
		TEST_CHECK(bs > 0);
		total += bs;
	}
	int const bs = merger.merge(piece_block(1_piece, 0), 4 * block_size);
	TEST_CHECK(bs > 0);
	total += bs;
	TEST_EQUAL(total, 5 * block_size);
}

// crossing into a new piece right after a *truncated* piece (piece size
// shorter than piece_length, i.e. the last piece of a file in a v2-only
// torrent) must not merge: the bytes that follow, alignment padding or
// another file's data, aren't contiguous with it in the byte stream a peer
// can actually serve, even though the piece indices are consecutive.
TORRENT_TEST(merge_stops_after_truncated_piece)
{
	// the only (truncated) block of piece 0
	request_merger merger(piece_block(0_piece, 0), 48, 4 * block_size, block_size);
	TEST_CHECK(!merger.continues(piece_block(1_piece, 0)));
}

// a truncated piece followed by more of the *same* piece is unaffected;
// the truncation only blocks merging past the piece boundary, not within it
TORRENT_TEST(merge_within_truncated_piece_unaffected)
{
	// piece truncated to 1.5 blocks worth: block 0 full, block 1 half
	int const truncated_piece_size = block_size + block_size / 2;
	request_merger merger(
		piece_block(0_piece, 0), truncated_piece_size, 2 * block_size, block_size);
	int const bs = merger.merge(piece_block(0_piece, 1), truncated_piece_size);
	TEST_EQUAL(bs, block_size / 2);
}

// a gap in flat block order (e.g. block 2 requested right after block 0,
// skipping block 1) never continues a run; merge() requires continues()
// to hold, so callers must check this first, not call merge() to find
// out
TORRENT_TEST(merge_stops_on_gap)
{
	request_merger merger(piece_block(0_piece, 0), 4 * block_size, 4 * block_size, block_size);
	TEST_CHECK(!merger.continues(piece_block(0_piece, 2))); // skips block 1
}

// a gap that happens to land exactly on the next piece (piece skipped
// entirely) also never continues a run
TORRENT_TEST(merge_stops_on_piece_gap)
{
	// last block of piece 0 (4 blocks/piece)
	request_merger merger(piece_block(0_piece, 3), 4 * block_size, 4 * block_size, block_size);
	TEST_CHECK(!merger.continues(piece_block(2_piece, 0))); // skips piece 1 entirely
}
