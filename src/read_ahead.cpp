/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include "libtorrent/aux_/read_ahead.hpp"
#include "libtorrent/performance_counters.hpp"

namespace libtorrent::aux {

namespace {

int clamp_to_int(std::int64_t const v)
{
	return int(std::clamp<std::int64_t>(
		v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

}

void read_ahead_predictor::leave_piece(counters& stats)
{
	if (!m_context.valid)
		return;
	// only blocks actually advised to the disk backend can be wasted work;
	// hinted_until is just the confidence bet, which may run ahead of that
	// without costing anything if it's never translated into a real advise
	int const effective_advised = std::min(m_context.advised_until, m_context.last_valid_block);
	int const wasted = effective_advised - m_context.max_block_requested;
	if (wasted <= 0)
		return;
	m_confidence /= 2;
	stats.inc_stats_counter(counters::num_read_ahead_wasted_blocks, wasted);
}

std::pair<int, bool> read_ahead_predictor::on_request(piece_index_t const piece,
	int const block_index,
	int const blocks_in_piece,
	time_point const now,
	counters& stats)
{
	bool const new_piece = !m_context.valid || m_context.piece != piece;
	if (new_piece)
	{
		leave_piece(stats);
		m_context = piece_context{};
		m_context.valid = true;
		m_context.piece = piece;
		m_context.last_valid_block = blocks_in_piece - 1;
	}

	piece_context& ctx = m_context;
	bool const first_sample = (ctx.max_block_requested == -1);

	// hot: this block had already been handed to the disk backend as an
	// actual read-ahead hint, regardless of whether the request pattern
	// itself is sequential enough to grow or shrink confidence below, and
	// regardless of whether this turns out to be a duplicate or backward
	// request: those are not a signal for the predictor, but the block
	// they name was, or wasn't, genuinely prefetched either way. cold: it
	// hadn't, which is always true for a piece's first request, since
	// nothing is ever pre-advised across a piece boundary. this is an
	// approximation, not exact: advised_until is a single forward
	// high-water mark, not a record of every byte range ever advised, so
	// a request far enough behind it can still be wrongly called hot if
	// that particular block was actually served by an ordinary read
	// rather than a hint. it's accurate for the common, roughly-monotonic
	// case this predictor is built around.
	if (block_index <= ctx.advised_until)
		stats.inc_stats_counter(counters::num_read_ahead_hot_blocks);
	else
		stats.inc_stats_counter(counters::num_read_ahead_cold_blocks);

	// neither of these is a genuine step, so neither may feed the cadence
	// tracking below: a near-instantaneous duplicate must not be mistaken
	// for a fast request rate
	if (!first_sample && block_index == ctx.max_block_requested)
	{
		// exact repeat (cancel/re-request, endgame mode): ignored
		return {0, new_piece};
	}

	if (!first_sample && block_index < ctx.max_block_requested)
	{
		// backward request: penalize proportionally, the same magnitude as
		// an unconsumed bet on leaving a piece. the outstanding hint is
		// left in place, so leaving this piece can still judge whether it
		// was eventually consumed
		m_confidence /= 2;
		return {0, new_piece};
	}

	if (!first_sample)
	{
		std::int64_t const ms = total_milliseconds(now - ctx.last_request_time);
		ctx.interval.add_sample(clamp_to_int(ms));
	}
	ctx.last_request_time = now;

	if (first_sample)
	{
		ctx.max_block_requested = block_index;
		int const window = m_confidence;
		if (window > 0)
			ctx.hinted_until = block_index + window;
		// a bet may reach past the end of this piece; that's tracked above
		// via hinted_until for cross-piece confidence, but a single
		// read-ahead hint can't span into the next piece
		int const clamped = std::min(window, ctx.last_valid_block - block_index);
		if (clamped <= 0)
			return {0, new_piece};
		stats.inc_stats_counter(counters::num_read_ahead_issued_blocks, clamped);
		ctx.advised_until = block_index + clamped;
		return {clamped, new_piece};
	}

	// forward step, gaps allowed. only a request strictly back-to-back with
	// the last one demonstrates sequential access, worth growing confidence
	// over; landing inside the hinted window by skipping ahead confirms the
	// bet without demonstrating that, so it's left neutral rather than
	// rewarded; landing outside the hinted window shrinks confidence, the
	// same penalty as leaving a piece with the bet unconsumed. this is
	// independent of the num_read_ahead_hot_blocks/cold_blocks counters
	// above, which track whether the backend was actually told about this
	// block in advance, not whether the access pattern looks sequential
	bool const back_to_back = block_index == ctx.max_block_requested + 1;
	bool const within_window = block_index <= ctx.hinted_until;
	if (back_to_back)
		m_confidence = std::min(max_window, m_confidence + forward_bump);
	else if (!within_window)
		m_confidence /= 2;

	ctx.gap.add_sample(block_index - ctx.max_block_requested);
	ctx.max_block_requested = block_index;

	bool const rate_ok =
		ctx.interval.num_samples() == 0 || milliseconds(ctx.interval.mean()) <= residency_budget;

	int const window = rate_ok ? m_confidence / ctx.gap.mean() : 0;

	if (window > 0)
		ctx.hinted_until = std::max(ctx.hinted_until, block_index + window);

	// refill once the previously advised runway drops to half the current
	// window, not once it's exhausted: triggering only when fully drained
	// would hand the kernel a hint with no lead time left, stalling the
	// pipeline at every window boundary. this re-advises the outstanding
	// half each time, trading bounded duplication for keeping the backend
	// ahead of the reader
	if (ctx.advised_until - block_index > window / 2)
		return {0, new_piece};

	// a bet may reach past the end of this piece; that's tracked above via
	// hinted_until for cross-piece confidence, but a single read-ahead
	// hint can't span into the next piece
	int const clamped = std::min(window, ctx.last_valid_block - block_index);
	if (clamped <= 0)
		return {0, new_piece};
	stats.inc_stats_counter(counters::num_read_ahead_issued_blocks, clamped);
	ctx.advised_until = block_index + clamped;
	return {clamped, new_piece};
}

}
