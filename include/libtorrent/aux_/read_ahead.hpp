/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_READ_AHEAD_HPP_INCLUDED
#define TORRENT_READ_AHEAD_HPP_INCLUDED

#include "libtorrent/units.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/sliding_average.hpp"

namespace libtorrent {

struct counters;

namespace aux {

// predicts how far a disk backend should speculatively read ahead for a
// peer connection, from the order it requests blocks within a piece; which
// piece is requested doesn't matter. confidence is expressed directly in
// blocks, doubles on each confirmed hit, and persists across piece
// transitions, so an established peer keeps getting prefetched starting
// from the first request of each new piece.
//
// elapsed time substitutes for real page-cache feedback: a bet counts as a
// hit only if it's redeemed within residency_budget, and that budget also
// caps the bet size when a peer's request rate is too slow for a bet to
// pay off. a consistent gap between requests shrinks the bet proportionally
// (confidence divided by the average gap), rather than zeroing it out.
struct TORRENT_EXTRA_EXPORT read_ahead_predictor
{
	// records a block request and returns the read-ahead window, in blocks
	// (0 for none), for the disk backend to act on, already clamped to the
	// end of the piece; confidence accounting internally tracks bets past
	// that point for cross-piece continuity, but a single hint can't span
	// into the next piece. now is passed in explicitly, to keep this class
	// pure and testable. updates the num_read_ahead_* counters as a side
	// effect, for instrumentation.
	int on_request(
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

		// blocks_in_piece - 1, fixed at creation
		int last_valid_block = -1;

		// when hinted_until was last extended
		time_point hint_issued_at{};

		time_point last_request_time{};

		// block-index gap
		aux::sliding_average<int, 8> gap;

		// milliseconds between requests
		aux::sliding_average<int, 8> interval;
	};

	// halves confidence for an unconsumed bet, clamped to blocks that
	// actually existed in the evicted piece.
	void account_for_eviction(piece_context const& evicted, counters& stats);

	piece_context& find_or_create(piece_index_t piece, int blocks_in_piece, counters& stats);

	aux::array<piece_context, 2> m_contexts;

	// the read-ahead window this connection has earned, in blocks,
	// persistent across pieces for the connection's lifetime
	int m_confidence = 0;

	static constexpr int max_window = 128;

	// growth for an ordinary (unconfirmed) forward step; a confirmed hit
	// instead doubles confidence
	static constexpr int forward_bump = 1;

	// how long a speculative block may plausibly sit before it's stale,
	// used both to size future bets and to judge past hits. a rough guess
	// pending real-world tuning.
	static constexpr milliseconds residency_budget{2000};
};

} // namespace aux
}

#endif
