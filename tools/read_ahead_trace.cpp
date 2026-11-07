/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// replays synthetic block-request patterns through read_ahead_predictor and
// writes a trace to stdout for tools/read_ahead_viz.py to render. each
// scenario is a "scenario <id> <blocks-in-piece>" line, a title line, a
// summary line, then one line per request:
//
//   piece block time-ms first-block window new-piece confidence
//   advised-until issued-blocks wasted-blocks hot
//
// first-block and window are the hint returned: the first block advised
// and how many, 0 0 for none
//
// terminated by a blank line

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <random>
#include <algorithm>
#include <chrono>

#include "libtorrent/performance_counters.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/read_ahead.hpp"

using namespace lt;
using namespace std::chrono_literals;

struct req
{
	int piece;
	int block;
	milliseconds dt;
};

struct scenario
{
	std::string id;
	std::string title;
	std::string summary;
	int blocks_in_piece;
	std::function<std::vector<req>()> gen;
};

static std::vector<req> seq(
	int first_piece, int num_pieces, int bip, milliseconds dt, int from = 0, int to = -1)
{
	std::vector<req> r;
	if (to < 0)
		to = bip - 1;
	for (int p = first_piece; p < first_piece + num_pieces; ++p)
		for (int b = from; b <= to; ++b)
			r.push_back({p, b, dt});
	return r;
}

int main()
{
	std::vector<scenario> const scenarios = {
		{"sequential",
			"Plain sequential",
			"One peer reads three 64-block pieces front to back at a 10 ms cadence. "
			"Confidence climbs one block per request, hints are re-issued once the "
			"advised runway has drained to half the window, and the earned window "
			"carries straight into the first request of the next piece. A hint "
			"is clamped at the piece boundary, and once the advised runway "
			"reaches it nothing more is issued for that piece.",
			64,
			[] { return seq(0, 3, 64, 10ms); }},

		{"small_pieces",
			"Sequential over small pieces",
			"The same reader over 16-block pieces. Confidence keeps growing past "
			"the piece size, but each hint is clamped at the end of its piece, so "
			"the backend only ever sees the piece-sized window. Every first request "
			"of a piece gets a full-piece hint plus the new_piece flag.",
			16,
			[] { return seq(0, 8, 16, 10ms); }},

		{"random",
			"Uniformly random blocks",
			"60 requests for random blocks in random pieces. Nothing is ever "
			"back-to-back, so confidence never leaves zero and no hint is issued. "
			"Every request is a piece switch with nothing to waste.",
			64,
			[] {
				std::mt19937 rng(7);
				std::vector<req> r;
				for (int i = 0; i < 60; ++i)
					r.push_back({int(rng() % 6), int(rng() % 64), 10ms});
				return r;
			}},

		{"stride2",
			"Every other block",
			"A reader that requests blocks 0, 2, 4, ... never produces a "
			"back-to-back pair. No hit is ever credited, confidence stays at zero "
			"and nothing is hinted, even though the pattern is perfectly "
			"predictable.",
			64,
			[] {
				std::vector<req> r;
				for (int p = 0; p < 2; ++p)
					for (int b = 0; b < 64; b += 2)
						r.push_back({p, b, 10ms});
				return r;
			}},

		{"reverse",
			"Reverse sequential",
			"Blocks requested from the end of the piece backwards. Every request "
			"after the first is a backward step and halves confidence, which is "
			"already zero, so this costs nothing but also earns nothing.",
			64,
			[] {
				std::vector<req> r;
				for (int p = 0; p < 2; ++p)
					for (int b = 63; b >= 0; --b)
						r.push_back({p, b, 10ms});
				return r;
			}},

		{"redundant",
			"Duplicates and re-requests",
			"Sequential reading where every block is requested twice (cancel and "
			"re-request) and every eighth request goes back three blocks. Exact "
			"repeats are ignored outright. Each backward step halves confidence, "
			"but the outstanding hint stays in place and keeps serving the "
			"following requests as hot.",
			64,
			[] {
				std::vector<req> r;
				for (int b = 0; b < 48; ++b)
				{
					r.push_back({0, b, 10ms});
					r.push_back({0, b, 0ms});
					if (b % 8 == 7)
						r.push_back({0, b - 3, 10ms});
				}
				return r;
			}},

		{"slow_then_fast",
			"Slow cadence, then a burst",
			"A sequential reader at one request every 3 s for 40 blocks, then at "
			"10 ms. While the 8-sample average interval exceeds the 2 s residency "
			"budget every request counts as a miss, so confidence stays at zero. "
			"The burst needs a few requests to pull the average under budget, "
			"after which confidence grows one block per request and hinting "
			"starts.",
			256,
			[] {
				std::vector<req> r;
				for (int b = 0; b < 40; ++b)
					r.push_back({0, b, 3s});
				for (int b = 40; b < 120; ++b)
					r.push_back({0, b, 10ms});
				return r;
			}},

		{"slow_sequential",
			"Slow sequential across pieces",
			"A sequential reader at one request every 3 s over two 32-block "
			"pieces. The average interval sits far above the residency budget, "
			"so every request counts as a miss: confidence never leaves zero and "
			"nothing is hinted, in the first piece or at the start of the second.",
			32,
			[] { return seq(0, 2, 32, 3s); }},

		{"bursty",
			"Pipelined bursts",
			"A reader that requests 16 blocks at once and then waits 3 s for "
			"them to arrive, over three 64-block pieces. Every burst starts with "
			"a 3 s gap, but the average interval stays well under the residency "
			"budget, so the peer is judged by its throughput: confidence grows "
			"and hints are issued as for a steady fast reader.",
			64,
			[] {
				std::vector<req> r;
				for (int p = 0; p < 3; ++p)
					for (int b = 0; b < 64; ++b)
						r.push_back({p, b, b % 16 == 0 ? 3s : 1ms});
				return r;
			}},

		{"idle_reset",
			"A long pause mid-piece",
			"A fast sequential reader stops for 31 s halfway through a piece and "
			"then continues where it left off. The silence exceeds the idle "
			"cutoff, so everything is discarded: confidence drops to zero, the "
			"resumed request is treated as the first request of a piece, and "
			"trust is re-earned one block at a time.",
			64,
			[] {
				auto r = seq(0, 1, 64, 10ms, 0, 31);
				r.push_back({0, 32, 31s});
				for (int b = 33; b < 64; ++b)
					r.push_back({0, b, 10ms});
				return r;
			}},

		{"hop_after_trust",
			"Piece hopping after earned trust",
			"A full sequential piece earns a 63-block window. The peer then "
			"requests only block 0 of each of the next eight pieces. Each first "
			"request is hinted with the whole current window; abandoning the "
			"piece is then a miss that halves confidence, with the whole hint "
			"counted as wasted. The waste shrinks geometrically: 63, 32, 16, 8, "
			"4, 2, 1, then nothing.",
			64,
			[] {
				auto r = seq(0, 1, 64, 10ms);
				for (int p = 1; p <= 8; ++p)
					r.push_back({p, 0, 10ms});
				return r;
			}},

		{"stride4_after_trust",
			"Stride 4 after earned trust",
			"Confidence is earned on 32 sequential blocks, then the reader "
			"switches to every fourth block. Leaving the first piece wastes the "
			"advised tail past block 31. The first strides land inside the "
			"15-block hint carried into the new piece and are neutral, but the "
			"window is divided by the average gap, so it shrinks to 3 blocks, "
			"smaller than the stride. From then on every request is a miss and "
			"confidence halves to zero within four requests. The three unread "
			"blocks between each pair of strides are never counted as wasted, "
			"since only blocks past the highest request count on leaving.",
			64,
			[] {
				auto r = seq(0, 1, 64, 10ms, 0, 31);
				for (int p = 1; p <= 2; ++p)
					for (int b = 0; b < 64; b += 4)
						r.push_back({p, b, 10ms});
				r.push_back({3, 0, 10ms});
				return r;
			}},

		{"bursts",
			"Short runs scattered across pieces",
			"Four back-to-back blocks per piece at a random offset, across twelve "
			"pieces. Each run earns three blocks of confidence; each departure "
			"is a miss that halves it again, with the blocks advised beyond the "
			"run counted as wasted. "
			"Confidence oscillates in a narrow band and hints stay a few blocks "
			"long.",
			64,
			[] {
				std::mt19937 rng(3);
				std::vector<req> r;
				for (int p = 0; p < 12; ++p)
				{
					int const start = int(rng() % 56);
					for (int b = start; b < start + 4; ++b)
						r.push_back({p, b, 10ms});
				}
				return r;
			}},

		{"interleaved",
			"Two pieces interleaved",
			"A pipelined client alternating requests between two pieces (A0, B0, "
			"A1, B1, ...). Every request is a piece switch, so the single-cursor "
			"predictor resets its context each time, never sees a back-to-back "
			"pair, and hints nothing, although each piece is being read strictly "
			"sequentially.",
			64,
			[] {
				std::vector<req> r;
				for (int b = 0; b < 32; ++b)
				{
					r.push_back({0, b, 10ms});
					r.push_back({1, b, 10ms});
				}
				return r;
			}},

		{"skips",
			"Sequential with occasional skips",
			"A sequential reader that skips one block every ten (the block was "
			"already sent or cancelled). The skip is not back-to-back, so it earns "
			"nothing, but it lands on a block already advised, so it costs nothing "
			"either. The gap average stays close to one and the window is barely "
			"affected.",
			64,
			[] {
				std::vector<req> r;
				for (int p = 0; p < 2; ++p)
					for (int b = 0; b < 64; ++b)
						if (b % 10 != 5)
							r.push_back({p, b, 10ms});
				return r;
			}},

		{"max_window",
			"Reaching the 64-block cap",
			"A long sequential read over 512-block pieces. Confidence stops at the "
			"64-block cap and hints are re-issued every 32 blocks from then on, "
			"each covering only the 32 blocks past the outstanding runway.",
			512,
			[] { return seq(0, 1, 512, 10ms, 0, 400); }},
	};

	for (auto const& s : scenarios)
	{
		aux::read_ahead_predictor p;
		counters stats;
		time_point t = time_point{} + 1s;

		std::printf("scenario %s %d\ntitle %s\nsummary %s\n",
			s.id.c_str(),
			s.blocks_in_piece,
			s.title.c_str(),
			s.summary.c_str());

		for (auto const& r : s.gen())
		{
			t += r.dt;
			auto const issued0 = stats[counters::num_read_ahead_issued_blocks];
			auto const wasted0 = stats[counters::num_read_ahead_wasted_blocks];
			auto const hot0 = stats[counters::num_read_ahead_hot_blocks];

			auto const [hint, new_piece] =
				p.on_request(piece_index_t(r.piece), r.block, s.blocks_in_piece, t, stats);

			std::printf("%d %d %lld %d %d %d %d %d %lld %lld %d\n",
				r.piece,
				r.block,
				(long long)total_milliseconds(t - time_point{}),
				hint.first_block,
				hint.num_blocks,
				int(new_piece),
				p.confidence(),
				p.advised_until(),
				(long long)(stats[counters::num_read_ahead_issued_blocks] - issued0),
				(long long)(stats[counters::num_read_ahead_wasted_blocks] - wasted0),
				int(stats[counters::num_read_ahead_hot_blocks] - hot0));
		}
		std::printf("\n");
	}
}
