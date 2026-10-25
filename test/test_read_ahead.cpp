/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/read_ahead.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/performance_counters.hpp"

using namespace lt;
using namespace std::chrono_literals;

namespace {

piece_index_t const piece_a{0};
piece_index_t const piece_b{1};
piece_index_t const piece_c{2};

} // anonymous namespace

// a tight contiguous run within one piece should double the window on each
// confirmed hit, up to its cap
TORRENT_TEST(contiguous_run_grows_window)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats), 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats), 1);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 1000, t, stats), 2);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 3, 1000, t, stats), 4);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 4, 1000, t, stats), 8);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 5, 1000, t, stats), 16);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 6, 1000, t, stats), 32);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 7, 1000, t, stats), 64);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 8, 1000, t, stats), 128);

	// window is now saturated
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 9, 1000, t, stats), 128);
}

// a consistent gap between requests shrinks the window proportionally
// (confidence divided by the gap), rather than zeroing it out, this is the
// over-reading guard
TORRENT_TEST(sparse_gap_keeps_window_small)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats), 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 4, 1000, t, stats), 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 8, 1000, t, stats), 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 12, 1000, t, stats), 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 16, 1000, t, stats), 1);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 20, 1000, t, stats), 1);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 24, 1000, t, stats), 1);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 28, 1000, t, stats), 1);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 32, 1000, t, stats), 2);

	// confidence has grown to 8 purely from unconfirmed forward steps (no
	// hit ever landed, since the gap kept outrunning the hinted range), but
	// the per-request window stayed tiny throughout; the carried-over
	// window on a fresh piece reveals the real confidence directly
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats), 8);
}

// a backward request is treated as a partial miss, not a full reset:
// confidence is halved (the same proportional penalty account_for_eviction
// uses for an unconsumed bet at eviction), and the outstanding hint is left
// in place rather than collapsed
TORRENT_TEST(backward_request_halves_window)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // window reached 2
	t += 10ms;
	p.on_request(piece_a, 3, 1000, t, stats); // confidence doubles to 4, hinted up to 7

	t += 10ms;
	TEST_EQUAL(
		p.on_request(piece_a, 2, 1000, t, stats), 0); // backward: no window, confidence halved to 2

	// the hint (up to block 7) was left standing, so this request still
	// lands inside it and counts as a fresh hit, doubling confidence again
	// (2 -> 4), rather than the 0 -> 1 a full reset would have produced
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 4, 1000, t, stats), 4);
}

// since a backward request no longer permanently exempts a piece from
// eviction accounting, a bet left outstanding after a backward blip can
// still be judged, and penalized, by a later eviction
TORRENT_TEST(eviction_still_penalizes_bet_outstanding_after_backward)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // hinted up to block 4, confidence 2

	// a backward blip: halves confidence (2 -> 1), but the outstanding bet
	// (hinted up to block 4) is left in place
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);

	// touch piece_b once: carries piece_a into the LRU slot without
	// evicting it yet
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats), 1);

	// touching a third piece evicts piece_a; its bet (hinted up to block 4)
	// was never consumed (only blocks 0-2 were ever requested), so the
	// eviction penalty still applies despite the earlier backward request
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_c, 0, 1000, t, stats), 0); // halved again: 1 -> 0
}

// an exact repeat (cancel/re-request, endgame mode) is not a signal either
// way
TORRENT_TEST(duplicate_request_is_a_no_op)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats), 0); // duplicate: no-op

	// the next real step behaves exactly as it would have without the
	// duplicate (compare with contiguous_run_grows_window's second request)
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 1000, t, stats), 2);
}

// confidence carries over across a clean piece jump (no backward request),
// giving an established peer a nonzero hint on the very first request of a
// new piece, unlike a connection with no history
TORRENT_TEST(confidence_carries_over_piece_jump)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats);

	// jump to an entirely different piece: the first request there still
	// gets a nonzero hint, seeded from the carried-over confidence
	t += 10ms;
	TEST_CHECK(p.on_request(piece_b, 0, 1000, t, stats) > 0);

	// a fresh predictor has no history and gets nothing on its first request
	aux::read_ahead_predictor fresh;
	TEST_EQUAL(fresh.on_request(piece_b, 0, 1000, t, stats), 0);
}

// a request landing inside a previously-hinted range, arriving promptly,
// doubles confidence
TORRENT_TEST(prompt_hit_earns_larger_bump)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // window 2, hints up to block 4

	t += 100ms; // well within the residency budget
	TEST_EQUAL(p.on_request(piece_a, 3, 1000, t, stats), 4); // confidence doubles: 2 -> 4
}

// the same in-range request, arriving after the residency budget has
// elapsed, is treated as an ordinary forward step instead of a hit
TORRENT_TEST(stale_hit_does_not_earn_bonus)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // window 2, hints up to block 4

	t += 3s; // beyond the residency budget
	TEST_EQUAL(p.on_request(piece_a, 3, 1000, t, stats), 3); // ordinary forward step: 2 + 1
}

// a piece context evicted with a genuinely unconsumed hinted range halves
// confidence, inferred purely from later on_request() calls, no
// backend/cache involvement needed
TORRENT_TEST(eviction_with_unconsumed_bet_is_penalized)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	// build up piece_a to the point where it has an outstanding, unconsumed
	// bet (hinted up to block 4, but only blocks 0-2 were ever requested)
	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats);

	// touch piece_b once: this carries piece_a over into the LRU slot, but
	// doesn't evict it yet (there are two slots). its first-request window
	// reflects the confidence built up on piece_a.
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats), 2);

	// touching a third piece evicts piece_a, which still had an unconsumed
	// bet; the penalty halves the carried-over window seen here, versus
	// piece_b above
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_c, 0, 1000, t, stats), 1);
}

// if the only unconsumed part of a hinted range was past the piece's
// actual last block, no penalty applies, those blocks never existed to be
// requested in the first place
TORRENT_TEST(eviction_past_piece_end_is_not_penalized)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	// piece_a has only 3 blocks (0, 1, 2); the hint issued at block 2
	// reaches for block 4, which doesn't exist
	p.on_request(piece_a, 0, 3, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 3, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 3, t, stats);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats), 2);

	// evicting piece_a doesn't change the carried-over window, no penalty
	// applied, since the unconsumed part of its bet never really existed
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_c, 0, 1000, t, stats), 2);
}

// a near-instantaneous duplicate must not corrupt the cadence tracking used
// by the slow-cadence guard: a genuinely slow forward step right behind it
// must still collapse the window to 0
TORRENT_TEST(duplicate_request_does_not_mask_slow_cadence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);

	// a near-instantaneous duplicate (cancel/re-request in endgame mode)
	t += 1ms;
	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats), 0); // duplicate: no-op

	// the real cadence is much slower than the residency budget; the guard
	// must still collapse the window, the duplicate must not have made this
	// peer look fast
	t += 3s;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats), 0);
}

// a connection requesting in perfect forward order, but with a request
// cadence far slower than data could plausibly survive in cache, gets no
// read-ahead at all, despite perfect ordering and saturated confidence.
// this is also what keeps a deliberately slow-but-sequential peer from
// being able to trigger repeated wasted prefetch I/O.
TORRENT_TEST(slow_cadence_collapses_window_despite_perfect_order)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	int window = 0;
	for (int block = 1; block <= 8; ++block)
	{
		t += 10ms;
		window = p.on_request(piece_a, block, 1000, t, stats);
	}
	TEST_EQUAL(window, 128); // window fully saturated

	// a single, much larger gap is enough to pull the interval average
	// above the residency budget
	t += 20s;
	TEST_EQUAL(p.on_request(piece_a, 9, 1000, t, stats), 0);
}
