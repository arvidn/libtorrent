/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_READ_AHEAD_HPP_INCLUDED
#define TORRENT_READ_AHEAD_HPP_INCLUDED

#include <utility>

#include "libtorrent/units.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/aux_/sliding_average.hpp"

namespace libtorrent {
struct counters;
}

namespace libtorrent::aux {

// predicts how far a disk backend should speculatively read ahead for a
// peer connection, from the order it requests blocks within a piece; which
// piece is requested doesn't matter. confidence is expressed directly in
// blocks and persists across piece transitions, so an established peer
// keeps getting prefetched starting from the first request of each new
// piece. a peer has a single read cursor: only one piece is tracked at a
// time, so a request for a different piece always means leaving the one
// being tracked.
//
// confidence grows linearly, one block at a time, and only for a request
// that is strictly back-to-back with the last one; a request that merely
// lands within the already-hinted window (because it skipped ahead, not
// because it continued) is neutral, neither earning nor costing
// confidence. a request landing outside that window is a miss and halves
// confidence, the same penalty leaving a piece with an unconsumed window
// applies. growing by a fixed amount per genuine hit and shrinking
// multiplicatively on a miss means an established peer's confidence
// decays fast the moment it stops actually reading sequentially, rather
// than drifting upward on a pattern that merely resembles one, e.g. a
// peer that reads a few blocks per piece across many pieces, never
// sequentially, but happens to keep landing inside its own prior bet.
//
// a peer's request rate substitutes for real page-cache feedback: once
// the average gap between requests is slow enough that a speculative
// block would plausibly be evicted before it's used, the window this
// call returns is capped at 0, independent of confidence.
//
// confidence (the bet) is tracked separately from what's actually been
// handed to the disk backend: a hint is reissued once the previously
// advised runway shrinks to half its window, not once it's exhausted, so
// the backend stays ahead of the reader instead of catching up to empty
// runway and stalling. the cost is re-advising that outstanding half each
// time.
struct TORRENT_EXTRA_EXPORT read_ahead_predictor
{
	// records a block request and returns {read-ahead window, new_piece}.
	// the window is in blocks (0 for none), for the disk backend to act
	// on, already clamped to the end of the piece; confidence accounting
	// internally tracks bets past that point for cross-piece continuity,
	// but a single hint can't span into the next piece. it's 0 while more
	// than half of the previously issued hint's window is still
	// outstanding, even as confidence keeps growing underneath. new_piece
	// is true exactly for the first request of its piece; combined with a
	// nonzero window, it lets a disk backend get a head start on the
	// piece transition instead of waiting for this request's own disk
	// job to reach a disk thread. now is passed in explicitly, to keep
	// this class pure and testable. updates the num_read_ahead_* counters
	// as a side effect, for instrumentation.
	std::pair<int, bool> on_request(
		piece_index_t piece, int block_index, int blocks_in_piece, time_point now, counters& stats);

private:
	struct piece_context
	{
		bool valid = false;
		piece_index_t piece{0};

		// highest block index requested so far
		int max_block_requested = -1;

		// highest block index already bet on
		int hinted_until = -1;

		// highest block index the disk backend has actually been told to
		// prefetch. distinct from hinted_until: confidence keeps updating
		// every request, but a new advise is only issued once
		// advised_until - block_index shrinks to half the current window
		int advised_until = -1;

		// blocks_in_piece - 1, fixed at creation
		int last_valid_block = -1;

		time_point last_request_time{};

		// block-index gap
		aux::sliding_average<int, 8> gap;

		// milliseconds between requests
		aux::sliding_average<int, 8> interval;
	};

	// halves confidence if the piece being left still has advised blocks
	// that were never consumed, clamped to blocks that actually existed in
	// it. a no-op for a piece that was never requested (the predictor's
	// first call).
	void leave_piece(counters& stats);

	piece_context m_context;

	// the read-ahead window this connection has earned, in blocks,
	// persistent across pieces for the connection's lifetime
	int m_confidence = 0;

	static constexpr int max_window = 128;

	// confidence earned per genuine (back-to-back) hit
	static constexpr int forward_bump = 1;

	// how long a speculative block may plausibly sit before it's stale;
	// caps the bet when a peer's request rate is too slow for it to pay
	// off. a rough guess pending real-world tuning.
	static constexpr milliseconds residency_budget{2000};
};

} // namespace libtorrent::aux

#endif
