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

void check_hint(read_ahead_hint const h, int const first_block, int const num_blocks)
{
	TEST_EQUAL(h.first_block, first_block);
	TEST_EQUAL(h.num_blocks, num_blocks);
}

} // anonymous namespace

// a tight contiguous run within one piece grows confidence by one block
// per request; the half-window refill margin still means a fresh advise
// only surfaces once the previously issued one is mostly consumed, not on
// every single request. each hint starts right past the previous one, so
// the advised ranges tile the piece without overlap
TORRENT_TEST(contiguous_run_grows_window_linearly)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	check_hint(p.on_request(piece_a, 0, 1000, t, stats).first, 0, 0);

	t += 10ms;
	check_hint(p.on_request(piece_a, 1, 1000, t, stats).first, 2, 1); // advised through 2

	t += 10ms;
	check_hint(p.on_request(piece_a, 2, 1000, t, stats).first, 3, 2); // through 4

	t += 10ms;
	check_hint(p.on_request(piece_a, 3, 1000, t, stats).first, 5, 2); // through 6

	t += 10ms;
	check_hint(p.on_request(piece_a, 4, 1000, t, stats).first, 7, 2); // through 8

	t += 10ms;
	check_hint(p.on_request(piece_a, 5, 1000, t, stats).first, 0, 0); // runway 3 > 5 / 2

	t += 10ms;
	check_hint(p.on_request(piece_a, 6, 1000, t, stats).first, 9, 4); // through 12

	t += 10ms;
	check_hint(p.on_request(piece_a, 7, 1000, t, stats).first, 0, 0);

	t += 10ms;
	check_hint(p.on_request(piece_a, 8, 1000, t, stats).first, 13, 4); // through 16
	TEST_EQUAL(p.advised_until(), 16);
}

// over a long sequential run the hints are disjoint and leave no gap:
// each one starts right after the previous one ended
TORRENT_TEST(hints_tile_the_piece_without_overlap)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	// the first request (block 0) earns nothing and the second (block 1)
	// advises from the block after itself, so block 2 is the first advised
	int next_first = 2;
	int issued = 0;
	for (int block = 0; block < 1000; ++block)
	{
		auto const h = p.on_request(piece_a, block, 1000, t, stats).first;
		if (h.num_blocks > 0)
		{
			TEST_EQUAL(h.first_block, next_first);
			TEST_CHECK(h.first_block > block);
			next_first = h.first_block + h.num_blocks;
			issued += h.num_blocks;
		}
		t += 10ms;
	}
	// every block from 2 to the end was advised exactly once
	TEST_EQUAL(next_first, 1000);
	TEST_EQUAL(issued, 998);
	TEST_EQUAL(stats[counters::num_read_ahead_issued_blocks], 998);
}

// a gap between requests that's never back-to-back never earns a hit, so
// confidence stays at 0 indefinitely: landing inside a window you never
// actually bet on isn't possible here since nothing was ever hinted
TORRENT_TEST(sparse_gap_never_grows_confidence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats).first.num_blocks, 0);

	for (int block = 4; block <= 32; block += 4)
	{
		t += 10ms;
		TEST_EQUAL(p.on_request(piece_a, block, 1000, t, stats).first.num_blocks, 0);
	}

	// confidence is still exactly 0: a fresh piece reveals it directly
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats).first.num_blocks, 0);
}

// a backward request halves confidence, same as any other miss; recovery
// afterwards is linear, one block per back-to-back hit, same as building
// it up the first time
TORRENT_TEST(backward_request_halves_window_then_recovers_linearly)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block <= 10; ++block)
	{
		p.on_request(piece_a, block, 1000, t, stats); // confidence reaches 10
		t += 10ms;
	}

	TEST_EQUAL(p.on_request(piece_a, 5, 1000, t, stats).first.num_blocks,
		0); // backward: confidence halved, 10 -> 5
	t += 10ms;

	// the outstanding hint (through block 16) was left in place by the
	// backward step, so the margin still holds the next few requests back
	// even though confidence is growing again underneath (5 -> 6 -> 7 -> 8).
	// the refill then covers only the blocks past that outstanding hint
	TEST_EQUAL(p.on_request(piece_a, 11, 1000, t, stats).first.num_blocks, 0);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 12, 1000, t, stats).first.num_blocks, 0);
	t += 10ms;
	check_hint(p.on_request(piece_a, 13, 1000, t, stats).first, 17, 5);
	TEST_EQUAL(p.confidence(), 8);
}

// a request for a different piece that doesn't continue from the end of
// the one being tracked is a miss, judged immediately, since there's only
// one piece tracked at a time
TORRENT_TEST(abandoning_a_piece_is_a_miss)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block <= 5; ++block)
	{
		p.on_request(piece_a, block, 1000, t, stats); // confidence reaches 5
		t += 10ms;
	}

	// piece_a's hint reached well past block 5; abandoning it for piece_b
	// is a miss, halving confidence (5 -> 2) before piece_b is hinted
	auto const r = p.on_request(piece_b, 0, 1000, t, stats);
	check_hint(r.first, 1, 2);
	TEST_CHECK(r.second); // first request of a new piece
}

// continuing from a fully requested piece into block 0 of the next is a
// hit like any other back-to-back request; the part of the bet past the
// piece's actual last block never existed to be requested
TORRENT_TEST(continuing_into_the_next_piece_is_a_hit)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	// warm up some confidence on an unrelated, much larger piece; jumping
	// out of it later is a miss that halves confidence once (10 -> 5),
	// which is incidental to this test, not what it's checking
	for (int block = 0; block <= 10; ++block)
	{
		p.on_request(piece_c, block, 1000, t, stats);
		t += 10ms;
	}

	// piece_a has only 3 blocks (0, 1, 2); reading all of them consumes
	// the carried-over bet exactly, with nothing left over. the first
	// request's hint already covers the rest of the piece, so the
	// following requests have nothing left to advise
	TEST_EQUAL(p.on_request(piece_a, 0, 3, t, stats).first.num_blocks, 2);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 1, 3, t, stats).first.num_blocks, 0);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 3, t, stats).first.num_blocks, 0);
	t += 10ms;

	// confidence kept growing through piece_a's three back-to-back hits
	// (5 -> 6 -> 7) and the step into piece_b is one more (8): no second
	// halving on top of the one piece_c already incurred above
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats).first.num_blocks, 8);
}

// an exact repeat (cancel/re-request, endgame mode) is not a signal
// either way
TORRENT_TEST(duplicate_request_is_a_no_op)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats).first.num_blocks, 0); // duplicate: no-op

	// the next real step behaves exactly as it would have without the
	// duplicate
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 1000, t, stats).first.num_blocks, 2);
}

// confidence carries over across a clean piece jump (no backward
// request), giving an established peer a nonzero hint on the very first
// request of a new piece, unlike a connection with no history. leaving
// the jump out of piece_a is a miss (confidence 2 -> 1), but what's left
// is still carried into piece_b rather than lost
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
	TEST_CHECK(p.on_request(piece_b, 0, 1000, t, stats).first.num_blocks > 0);

	// a fresh predictor has no history and gets nothing on its first request
	aux::read_ahead_predictor fresh;
	TEST_EQUAL(fresh.on_request(piece_b, 0, 1000, t, stats).first.num_blocks, 0);
}

// landing on a block already advised to the backend by skipping ahead,
// rather than by continuing back-to-back, is neutral: it neither grows
// confidence (like a hit would) nor shrinks it (like a miss would).
// compared against a fully sequential run over the same number of
// requests, the skipped request is simply worth nothing, not a penalty
TORRENT_TEST(skip_onto_an_advised_block_is_neutral)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // confidence reaches 2, advised through block 4

	// block 3 is skipped; block 4 was already advised, but isn't
	// back-to-back with the previous request, so it's neutral
	t += 10ms;
	check_hint(p.on_request(piece_a, 4, 1000, t, stats).first, 5, 2); // confidence still 2
	TEST_EQUAL(p.confidence(), 2);
	// the skipped block 3 was advised, so it's wasted
	TEST_EQUAL(stats[counters::num_read_ahead_wasted_blocks], 1);

	// the next back-to-back request grows from the same confidence (2)
	// the skip left untouched, not from whatever it would have reached
	// by actually visiting blocks 3 and 4 in sequence
	t += 10ms;
	check_hint(p.on_request(piece_a, 5, 1000, t, stats).first, 7, 2);
	TEST_EQUAL(p.confidence(), 3);
}

// a request landing past everything advised so far is a genuine miss and
// halves confidence, the same penalty as leaving a piece with advised
// blocks unconsumed
TORRENT_TEST(request_past_the_advised_blocks_is_a_miss)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // confidence reaches 2

	// far beyond anything advised so far: a miss, confidence halves (2 -> 1)
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 100, 1000, t, stats).first.num_blocks, 0);

	// recovers linearly from the halved value, same as any other hit
	// (1 -> 2), though the gap average the jump dragged up still divides
	// the window down to nothing
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 101, 1000, t, stats).first.num_blocks, 0);
	TEST_EQUAL(p.confidence(), 2);

	// a fresh piece that doesn't continue piece_a is itself a miss (2 -> 1)
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats).first.num_blocks, 1);
}

// a near-instantaneous duplicate must not corrupt the cadence tracking
// used by the slow-cadence guard: a genuinely slow forward step right
// behind it must still collapse the window to 0, even though it's
// strictly back-to-back in block terms
TORRENT_TEST(duplicate_request_does_not_mask_slow_cadence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);

	// a near-instantaneous duplicate (cancel/re-request in endgame mode)
	t += 1ms;
	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats).first.num_blocks, 0); // duplicate: no-op

	// the real cadence, measured from the original request and not the
	// duplicate, is far slower than the residency budget
	t += 3s;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats).first.num_blocks, 0);
}

// a connection requesting in perfect back-to-back order, but with a
// request cadence far slower than data could plausibly survive in cache,
// gets no read-ahead at all, and the slow request costs it half the
// confidence it had established rather than earning it another hit.
// this is also what keeps a deliberately slow-but-sequential peer from
// being able to trigger repeated wasted prefetch I/O.
TORRENT_TEST(slow_cadence_collapses_window_despite_established_confidence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block <= 10; ++block)
	{
		p.on_request(piece_a, block, 100000, t, stats);
		t += 10ms;
	}

	// at a normal cadence, this is where the refill margin would next
	// surface a window: 11 blocks, 5 of which are already outstanding
	check_hint(p.on_request(piece_a, 11, 100000, t, stats).first, 17, 6);
	t += 10ms;

	for (int block = 12; block <= 14; ++block)
	{
		p.on_request(piece_a, block, 100000, t, stats);
		t += 10ms;
	}

	// a single, much larger gap right before the next such point is
	// enough to pull the interval average above the residency budget
	t += 20s;
	TEST_EQUAL(p.confidence(), 14);
	TEST_EQUAL(p.on_request(piece_a, 15, 100000, t, stats).first.num_blocks, 0);
	TEST_EQUAL(p.confidence(), 7);
}

// with more confidence than a piece has blocks, the hint is clamped at the
// piece end. the refill margin has to be judged against that clamped
// window: once advised_until sits at the last block the runway can only
// shrink, and comparing it to the full window would re-advise the already
// advised tail on every remaining request of the piece
TORRENT_TEST(advised_tail_is_not_reissued_at_piece_end)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	// the hint that reaches the end of the piece is the last one in it
	bool reached_end = false;
	for (int block = 0; block < 64; ++block)
	{
		int const window = p.on_request(piece_a, block, 64, t, stats).first.num_blocks;
		if (reached_end)
			TEST_EQUAL(window, 0);
		if (window > 0 && p.advised_until() == 63)
			reached_end = true;
		t += 10ms;
	}
	TEST_CHECK(reached_end);

	for (int block = 0; block < 64; ++block)
	{
		p.on_request(piece_b, block, 64, t, stats);
		t += 10ms;
	}
	TEST_EQUAL(p.confidence(), 64);

	// the window carried into a new piece covers the whole piece, so no
	// request after the first has anything left to advise
	check_hint(p.on_request(piece_c, 0, 64, t, stats).first, 1, 63);
	t += 10ms;
	auto const issued = stats[counters::num_read_ahead_issued_blocks];
	for (int block = 1; block < 64; ++block)
	{
		TEST_EQUAL(p.on_request(piece_c, block, 64, t, stats).first.num_blocks, 0);
		t += 10ms;
	}
	TEST_EQUAL(stats[counters::num_read_ahead_issued_blocks], issued);
}

// a reader slower than the residency budget never earns confidence: every
// request while the average is over budget halves what it has instead of
// crediting a hit, so there is nothing to hint at the start of the next
// piece either
TORRENT_TEST(slow_reader_never_earns_confidence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block < 16; ++block)
	{
		TEST_EQUAL(p.on_request(piece_a, block, 16, t, stats).first.num_blocks, 0);
		t += 3s;
	}
	TEST_EQUAL(p.confidence(), 0);
	TEST_EQUAL(p.on_request(piece_b, 0, 16, t, stats).first.num_blocks, 0);
}

// the average request interval spans piece switches of any kind. a single
// long stall at the end of a piece leaves the average over the residency
// budget, so the next requests count as misses whether they continue into
// the next piece or jump somewhere unrelated, until enough fast requests
// have pulled the average back under budget
TORRENT_TEST(slow_average_spans_piece_switches)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block < 16; ++block)
	{
		p.on_request(piece_a, block, 17, t, stats);
		t += 10ms;
	}
	t += 20s;
	p.on_request(piece_a, 16, 17, t, stats);
	TEST_EQUAL(p.confidence(), 7);
	t += 10ms;

	aux::read_ahead_predictor jump = p;
	TEST_EQUAL(jump.on_request(piece_c, 5, 1000, t, stats).first.num_blocks, 0);
	TEST_EQUAL(jump.confidence(), 3);

	TEST_EQUAL(p.on_request(piece_b, 0, 17, t, stats).first.num_blocks, 0);
	TEST_EQUAL(p.confidence(), 3);
	t += 10ms;
	// the average is back under budget: a hit again, and hinted
	TEST_EQUAL(p.on_request(piece_b, 1, 17, t, stats).first.num_blocks, 4);
	TEST_EQUAL(p.confidence(), 4);
}

// a peer silent for longer than the idle cutoff comes back as a stranger:
// confidence, cadence and the piece being tracked are all discarded, so
// its next request is treated like a predictor's very first one
TORRENT_TEST(long_idle_discards_all_state)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block < 32; ++block)
	{
		p.on_request(piece_a, block, 64, t, stats);
		t += 10ms;
	}
	TEST_EQUAL(p.confidence(), 31);

	t += 31s;
	auto const r = p.on_request(piece_a, 32, 64, t, stats);
	TEST_EQUAL(r.first.num_blocks, 0);
	TEST_CHECK(r.second);
	TEST_EQUAL(p.confidence(), 0);
	TEST_EQUAL(p.advised_until(), -1);

	// trust is re-earned from nothing, one block per request
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 33, 64, t, stats).first.num_blocks, 1);
	TEST_EQUAL(p.confidence(), 1);
}

// a gap short of the idle cutoff is an ordinary slow request: it pulls the
// average over budget, which halves the earned confidence rather than
// discarding it, and withholds the window until the average recovers
TORRENT_TEST(short_idle_halves_confidence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block < 32; ++block)
	{
		p.on_request(piece_a, block, 64, t, stats);
		t += 10ms;
	}

	t += 29s;
	TEST_EQUAL(p.on_request(piece_a, 32, 64, t, stats).first.num_blocks, 0);
	TEST_EQUAL(p.confidence(), 15);
}

// closing the connection mid-piece counts the outstanding runway as wasted
TORRENT_TEST(leaving_mid_piece_counts_runway_as_wasted)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // advised through block 4
	TEST_EQUAL(stats[counters::num_read_ahead_wasted_blocks], 0);

	p.leave_piece(stats);
	TEST_EQUAL(stats[counters::num_read_ahead_wasted_blocks], 2);
	TEST_EQUAL(p.advised_until(), -1);

	// leaving again has nothing left to count
	p.leave_piece(stats);
	TEST_EQUAL(stats[counters::num_read_ahead_wasted_blocks], 2);
}
