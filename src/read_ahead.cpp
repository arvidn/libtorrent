/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>
#include <cstdint>
#include <limits>

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

void read_ahead_predictor::account_for_eviction(piece_context const& evicted, counters& stats)
{
	if (!evicted.valid)
		return;
	int const effective_hinted = std::min(evicted.hinted_until, evicted.last_valid_block);
	int const wasted = effective_hinted - evicted.max_block_requested;
	if (wasted <= 0)
		return;
	m_confidence /= 2;
	stats.inc_stats_counter(counters::num_read_ahead_wasted_blocks, wasted);
}

read_ahead_predictor::piece_context& read_ahead_predictor::find_or_create(
	piece_index_t const piece, int const blocks_in_piece, counters& stats)
{
	if (m_contexts[0].valid && m_contexts[0].piece == piece)
		return m_contexts[0];
	if (m_contexts[1].valid && m_contexts[1].piece == piece)
	{
		std::swap(m_contexts[0], m_contexts[1]);
		return m_contexts[0];
	}

	// evict the least-recently-used slot, promote the other into its place
	account_for_eviction(m_contexts[1], stats);
	m_contexts[1] = m_contexts[0];
	m_contexts[0] = piece_context{};
	m_contexts[0].valid = true;
	m_contexts[0].piece = piece;
	m_contexts[0].last_valid_block = blocks_in_piece - 1;
	return m_contexts[0];
}

int read_ahead_predictor::on_request(piece_index_t const piece,
	int const block_index,
	int const blocks_in_piece,
	time_point const now,
	counters& stats)
{
	piece_context& ctx = find_or_create(piece, blocks_in_piece, stats);

	bool const first_sample = (ctx.max_block_requested == -1);

	// neither of these is a genuine step, so neither may feed the cadence
	// tracking below: a near-instantaneous duplicate must not be mistaken
	// for a fast request rate
	if (!first_sample && block_index == ctx.max_block_requested)
	{
		// exact repeat (cancel/re-request, endgame mode): ignored
		return 0;
	}

	if (!first_sample && block_index < ctx.max_block_requested)
	{
		// backward request: penalize proportionally, the same magnitude as
		// an unconsumed bet at eviction. the outstanding hint is left in
		// place so a later eviction can still judge whether it was consumed
		m_confidence /= 2;
		return 0;
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
		{
			ctx.hinted_until = block_index + window;
			ctx.hint_issued_at = now;
		}
		// a bet may reach past the end of this piece; that's tracked above
		// via hinted_until for cross-piece confidence, but a single
		// read-ahead hint can't span into the next piece
		int const clamped = std::min(window, ctx.last_valid_block - block_index);
		if (clamped <= 0)
			return 0;
		stats.inc_stats_counter(counters::num_read_ahead_issued_blocks, clamped);
		return clamped;
	}

	// forward step, gaps allowed
	bool const is_hit = block_index <= ctx.hinted_until;
	bool const fresh_hit = is_hit && (now - ctx.hint_issued_at) <= residency_budget;
	stats.inc_stats_counter(
		fresh_hit ? counters::num_read_ahead_hits : counters::num_read_ahead_misses);
	if (fresh_hit)
		m_confidence = m_confidence == 0 ? 1 : std::min(max_window, m_confidence * 2);
	else
		m_confidence = std::min(max_window, m_confidence + forward_bump);

	ctx.gap.add_sample(block_index - ctx.max_block_requested);
	ctx.max_block_requested = block_index;

	bool const rate_ok =
		ctx.interval.num_samples() == 0 || milliseconds(ctx.interval.mean()) <= residency_budget;

	int const window = rate_ok ? m_confidence / ctx.gap.mean() : 0;

	if (window > 0)
	{
		ctx.hinted_until = std::max(ctx.hinted_until, block_index + window);
		ctx.hint_issued_at = now;
	}

	// a bet may reach past the end of this piece; that's tracked above via
	// hinted_until for cross-piece confidence, but a single read-ahead
	// hint can't span into the next piece
	int const clamped = std::min(window, ctx.last_valid_block - block_index);
	if (clamped <= 0)
		return 0;
	stats.inc_stats_counter(counters::num_read_ahead_issued_blocks, clamped);
	return clamped;
}

}
