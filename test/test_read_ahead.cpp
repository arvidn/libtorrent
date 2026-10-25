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

// a tight contiguous run within one piece grows confidence by one block
// per request; the half-window refill margin still means a fresh advise
// only surfaces once the previously issued one is mostly consumed, not on
// every single request
TORRENT_TEST(contiguous_run_grows_window_linearly)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats).first, 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats).first, 1);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 1000, t, stats).first, 2);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 3, 1000, t, stats).first, 3);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 4, 1000, t, stats).first, 4);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 5, 1000, t, stats).first, 0); // still inside the block-4 hint

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 6, 1000, t, stats).first, 6);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 7, 1000, t, stats).first, 0);

	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 8, 1000, t, stats).first, 8);
}

// a gap between requests that's never back-to-back never earns a hit, so
// confidence stays at 0 indefinitely: landing inside a window you never
// actually bet on isn't possible here since nothing was ever hinted
TORRENT_TEST(sparse_gap_never_grows_confidence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats).first, 0);

	for (int block = 4; block <= 32; block += 4)
	{
		t += 10ms;
		TEST_EQUAL(p.on_request(piece_a, block, 1000, t, stats).first, 0);
	}

	// confidence is still exactly 0: a fresh piece reveals it directly
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats).first, 0);
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

	TEST_EQUAL(
		p.on_request(piece_a, 5, 1000, t, stats).first, 0); // backward: confidence halved, 10 -> 5
	t += 10ms;

	// the outstanding hint was left in place by the backward step, so the
	// margin still holds the next few requests back even though
	// confidence is growing again underneath (5 -> 6 -> 7 -> 8)
	TEST_EQUAL(p.on_request(piece_a, 11, 1000, t, stats).first, 0);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 12, 1000, t, stats).first, 0);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 13, 1000, t, stats).first, 8);
}

// leaving a piece with a genuinely unconsumed bet halves confidence,
// immediately, since there's only one piece tracked at a time: the
// moment a different piece is requested is the moment the one being left
// is judged
TORRENT_TEST(leaving_a_piece_with_unconsumed_window_halves_confidence)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	for (int block = 0; block <= 5; ++block)
	{
		p.on_request(piece_a, block, 1000, t, stats); // confidence reaches 5
		t += 10ms;
	}

	// piece_a's hint reached well past block 5; leaving it now wastes
	// that, halving confidence (5 -> 2) before piece_b even starts
	auto const r = p.on_request(piece_b, 0, 1000, t, stats);
	TEST_EQUAL(r.first, 2);
	TEST_CHECK(r.second); // first request of a new piece
}

// if the only unconsumed part of a bet was past the piece's actual last
// block, leaving it isn't penalized: those blocks never existed to be
// requested in the first place
TORRENT_TEST(leaving_a_piece_fully_consumed_is_not_penalized)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	// warm up some confidence on an unrelated, much larger piece; leaving
	// it later wastes most of its bet and halves confidence once (10 -> 5),
	// which is incidental to this test, not what it's checking
	for (int block = 0; block <= 10; ++block)
	{
		p.on_request(piece_c, block, 1000, t, stats);
		t += 10ms;
	}

	// piece_a has only 3 blocks (0, 1, 2); reading all of them consumes
	// the carried-over bet exactly, with nothing left over
	TEST_EQUAL(p.on_request(piece_a, 0, 3, t, stats).first, 2);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 1, 3, t, stats).first, 1);
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 3, t, stats).first, 0);
	t += 10ms;

	// confidence kept growing through piece_a's three back-to-back hits
	// (5 -> 6 -> 7) and carries over to piece_b undiminished: no second
	// halving on top of the one piece_c already incurred above
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats).first, 7);
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
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats).first, 0); // duplicate: no-op

	// the next real step behaves exactly as it would have without the
	// duplicate
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 2, 1000, t, stats).first, 2);
}

// confidence carries over across a clean piece jump (no backward
// request), giving an established peer a nonzero hint on the very first
// request of a new piece, unlike a connection with no history. leaving
// piece_a here does waste part of its bet (confidence 2 -> 1), but that's
// still carried into piece_b rather than lost
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
	TEST_CHECK(p.on_request(piece_b, 0, 1000, t, stats).first > 0);

	// a fresh predictor has no history and gets nothing on its first request
	aux::read_ahead_predictor fresh;
	TEST_EQUAL(fresh.on_request(piece_b, 0, 1000, t, stats).first, 0);
}

// landing inside the hinted window by skipping ahead, rather than by
// continuing back-to-back, is neutral: it neither grows confidence (like
// a hit would) nor shrinks it (like a miss would). compared against a
// fully sequential run over the same number of requests, the skipped
// request is simply worth nothing, not a penalty
TORRENT_TEST(skip_within_the_hinted_window_is_neutral)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // confidence reaches 2, hints well past block 2

	// block 3 is skipped; block 4 lands inside the window block 2 already
	// hinted, but isn't back-to-back with it, so it's neutral
	t += 10ms;
	TEST_EQUAL(
		p.on_request(piece_a, 4, 1000, t, stats).first, 2); // confidence still 2, just reclamped

	// the next back-to-back request grows from the same confidence (2)
	// the skip left untouched, not from whatever it would have reached
	// by actually visiting blocks 3 and 4 in sequence
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 5, 1000, t, stats).first, 3);
}

// a request landing outside the hinted window altogether is a genuine
// miss and halves confidence, the same penalty as leaving a piece with
// an unconsumed bet
TORRENT_TEST(request_outside_the_hinted_window_is_a_miss)
{
	aux::read_ahead_predictor p;
	time_point t = clock_type::now();
	counters stats;

	p.on_request(piece_a, 0, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 1, 1000, t, stats);
	t += 10ms;
	p.on_request(piece_a, 2, 1000, t, stats); // confidence reaches 2

	// far beyond anything hinted so far: a miss, confidence halves (2 -> 1)
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 100, 1000, t, stats).first, 0);

	// recovers linearly from the halved value, same as any other hit
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_a, 101, 1000, t, stats).first, 0); // margin still holds this back

	// reveal the recovered confidence (1 -> 2) via a fresh piece
	t += 10ms;
	TEST_EQUAL(p.on_request(piece_b, 0, 1000, t, stats).first, 2);
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
	TEST_EQUAL(p.on_request(piece_a, 0, 1000, t, stats).first, 0); // duplicate: no-op

	// the real cadence, measured from the original request and not the
	// duplicate, is far slower than the residency budget
	t += 3s;
	TEST_EQUAL(p.on_request(piece_a, 1, 1000, t, stats).first, 0);
}

// a connection requesting in perfect back-to-back order, but with a
// request cadence far slower than data could plausibly survive in cache,
// gets no read-ahead at all despite confidence being well-established:
// the slow-cadence guard caps the window independently of confidence.
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
	// surface a window
	TEST_EQUAL(p.on_request(piece_a, 11, 100000, t, stats).first, 11);
	t += 10ms;

	for (int block = 12; block <= 14; ++block)
	{
		p.on_request(piece_a, block, 100000, t, stats);
		t += 10ms;
	}

	// a single, much larger gap right before the next such point is
	// enough to pull the interval average above the residency budget
	t += 20s;
	TEST_EQUAL(p.on_request(piece_a, 15, 100000, t, stats).first, 0);
}
