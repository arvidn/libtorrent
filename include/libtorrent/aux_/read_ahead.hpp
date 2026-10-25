/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_READ_AHEAD_HPP_INCLUDED
#define TORRENT_READ_AHEAD_HPP_INCLUDED

#include <optional>
#include <utility>

#include "libtorrent/units.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/disk_interface.hpp" // for read_ahead_hint
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/aux_/sliding_average.hpp"

namespace libtorrent {
struct counters;
}

namespace libtorrent::aux {

// predicts how far a disk backend should speculatively read ahead for a
// peer connection, from the order and rate of its block requests.
//
// confidence, in blocks, is the window the connection has earned. it
// grows by one per request that is strictly back-to-back with the last
// one, block 0 of a new piece counting when the previous piece was
// requested through its last block, and halves on a request landing past
// what has been advised: a skip, a jump into a piece, a backward
// request. a skip onto an already advised block is neutral. linear
// growth and multiplicative decay keep a peer that merely resembles a
// sequential reader from drifting upward.
//
// the request rate stands in for page-cache feedback: while the average
// interval between requests exceeds the residency budget, prefetched
// data would likely be evicted before it's read, so every request counts
// as a miss and the window is 0. the average covers the whole request
// stream, across pieces, so a peer pipelining bursts of requests is
// judged by its throughput. a peer silent for longer than the idle
// cutoff starts over from scratch.
//
// only one piece is tracked at a time. a hint is reissued once the
// advised runway shrinks to half the window rather than once it's
// exhausted, so the backend stays ahead of the reader. each hint covers
// only blocks past what has already been advised: the kernel caps a
// single advise call by range, before skipping resident pages, so
// re-advising blocks it already has would eat into that cap.
struct TORRENT_EXTRA_EXPORT read_ahead_predictor
{
	// records a block request and returns {hint, new_piece}. the hint
	// names the blocks to advise (an empty range for none), clamped to the
	// end of the piece and disjoint from earlier hints on this piece.
	// new_piece is true for the first request of a piece; with a non-empty
	// hint it lets the backend start on the piece transition before this
	// request's own disk job reaches a disk thread. now is passed in to
	// keep the class pure and testable. updates the num_read_ahead_*
	// counters.
	std::pair<read_ahead_hint, bool> on_request(
		piece_index_t piece, int block_index, int blocks_in_piece, time_point now, counters& stats);

	// counts advised blocks past the last request as wasted and forgets the
	// piece. called when the connection closes, so its outstanding runway
	// is accounted for
	void leave_piece(counters& stats);

	// current state, for instrumentation (tools/read_ahead_trace.cpp)
	int confidence() const { return m_confidence; }
	int advised_until() const { return m_context ? m_context->advised_until : -1; }

private:
	struct piece_context
	{
		piece_context(piece_index_t const p, int const blocks_in_piece)
			: piece(p)
			, last_valid_block(blocks_in_piece - 1)
		{}

		piece_index_t piece;

		// highest block index requested so far
		int max_block_requested = -1;

		// highest block index the disk backend has been told to prefetch
		int advised_until = -1;

		int last_valid_block;

		// block-index gap
		aux::sliding_average<int, 8> gap;
	};

	std::optional<piece_context> m_context;

	// the read-ahead window this connection has earned, in blocks, kept
	// across pieces
	int m_confidence = 0;

	// milliseconds between forward requests, across pieces
	aux::sliding_average<int, 8> m_interval;
	time_point m_last_request_time{};

	// linux honors a single advise call only up to the larger of the
	// device's read_ahead_kb and max_sectors_kb, commonly 1280 kiB, so a
	// hint beyond that is silently truncated
	static constexpr int max_window = 64;
};

} // namespace libtorrent::aux

#endif
