/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>
#include <utility>

#include "libtorrent/aux_/read_ahead.hpp"
#include "libtorrent/performance_counters.hpp"

namespace libtorrent::aux {

namespace {

using namespace std::chrono_literals;

// how long a speculative block may plausibly sit before it's stale; caps
// the bet when a peer's request rate is too slow for it to pay off. a
// rough guess pending real-world tuning
constexpr milliseconds residency_budget = 2s;

// silence longer than this discards all state, confidence included
constexpr seconds idle_reset = 30s;
}

void read_ahead_predictor::leave_piece(counters& stats)
{
	if (!m_context)
		return;
	int const wasted = m_context->advised_until - m_context->max_block_requested;
	if (wasted > 0)
		stats.inc_stats_counter(counters::num_read_ahead_wasted_blocks, wasted);
}

std::pair<read_ahead_hint, bool> read_ahead_predictor::on_request(piece_index_t const piece,
	int const block_index,
	int const blocks_in_piece,
	time_point const now,
	counters& stats)
{
	if (m_context && now - m_last_request_time > idle_reset)
	{
		leave_piece(stats);
		m_context.reset();
		m_confidence = 0;
		m_interval = {};
	}

	bool const new_piece = !m_context || m_context->piece != piece;
	// a previous forward request exists whenever a piece has been tracked
	// since the last reset, whichever piece it was
	bool const timed = m_context.has_value();
	// a piece requested through its last block, followed by block 0 of the
	// next, continues the sequential read, whichever piece that is
	bool const continues_previous = m_context
		&& m_context->max_block_requested == m_context->last_valid_block && block_index == 0;
	if (new_piece)
	{
		leave_piece(stats);
		m_context.emplace(piece, blocks_in_piece);
	}

	piece_context& ctx = *m_context;

	// hot: the block had already been handed to the disk backend as a
	// read-ahead hint. counted even for a duplicate or backward request,
	// since the block was, or wasn't, prefetched either way. an
	// approximation: advised_until is a single high-water mark, so a
	// request far behind it that was served by an ordinary read is still
	// called hot
	bool const hot = block_index <= ctx.advised_until;
	if (hot)
		stats.inc_stats_counter(counters::num_read_ahead_hot_blocks);
	else
		stats.inc_stats_counter(counters::num_read_ahead_cold_blocks);

	// neither of these is a genuine step, so neither may feed the cadence
	// tracking below: a near-instantaneous duplicate must not be mistaken
	// for a fast request rate
	if (!new_piece && block_index == ctx.max_block_requested)
	{
		// exact repeat (cancel/re-request, endgame mode): ignored
		return {{}, new_piece};
	}

	if (!new_piece && block_index < ctx.max_block_requested)
	{
		// backward request: penalize proportionally, the same magnitude as
		// any other miss. the outstanding hint is left in place, so leaving
		// this piece can still count whether it was eventually consumed
		m_confidence /= 2;
		return {{}, new_piece};
	}

	// bounded by idle_reset, so it fits an int
	if (timed)
		m_interval.add_sample(int(total_milliseconds(now - m_last_request_time)));
	m_last_request_time = now;

	// the average smooths over a peer pipelining a burst of requests and
	// then waiting for the data. while it's over budget every request
	// counts as a miss, whatever block it names, so a slow but perfectly
	// sequential peer can't bank a window for later
	bool const rate_ok =
		m_interval.num_samples() == 0 || milliseconds(m_interval.mean()) <= residency_budget;
	if (!rate_ok)
		m_confidence /= 2;

	// a skip onto an already advised block cost nothing, so it's neutral.
	// a first request is never hot, so any start of a piece other than
	// continuing the previous one is a miss
	bool const back_to_back =
		new_piece ? continues_previous : block_index == ctx.max_block_requested + 1;
	if (back_to_back && rate_ok)
		m_confidence = std::min(max_window, m_confidence + 1);
	else if (!hot && rate_ok)
		// a slow miss has already paid once above
		m_confidence /= 2;

	if (!new_piece)
		ctx.gap.add_sample(block_index - ctx.max_block_requested);
	ctx.max_block_requested = block_index;

	// the gap average says nothing until the piece has seen a forward step
	int const gap = ctx.gap.num_samples() > 0 ? ctx.gap.mean() : 1;
	int const window = rate_ok ? m_confidence / gap : 0;

	// a single read-ahead hint can't span into the next piece
	int const clamped = std::min(window, ctx.last_valid_block - block_index);
	if (clamped <= 0)
		return {{}, new_piece};

	// refill once the advised runway drops to half the window, not once
	// it's exhausted: a hint with no lead time left stalls the pipeline at
	// every window boundary. judged against the clamped window: at the end
	// of the piece the runway can only shrink, and an unclamped window would
	// re-advise the tail on every remaining request
	if (ctx.advised_until - block_index > clamped / 2)
		return {{}, new_piece};

	// only the blocks past the outstanding runway, so the hint never
	// overlaps one already issued for this visit to the piece
	int const first = std::max(ctx.advised_until, block_index) + 1;
	ctx.advised_until = block_index + clamped;
	int const count = ctx.advised_until - first + 1;
	stats.inc_stats_counter(counters::num_read_ahead_issued_blocks, count);
	return {{first, count}, new_piece};
}

}
