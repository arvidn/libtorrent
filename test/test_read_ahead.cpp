/*

Copyright (c) 2026, ghz
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/read_ahead.hpp"
#include "libtorrent/time.hpp"
#include "test.hpp"

#include <string>

using lt::aux::read_ahead;

namespace {

constexpr int kib = 1024;
constexpr int mib = 1024 * kib;

lt::storage_index_t const st{0};
lt::piece_index_t const piece{0};
lt::aux::steady_clock::duration const ttl = lt::seconds(1);

// what a read of `p` does, as a string: "none", "lookup", or the hints as
// "offset+length ..." in kiB. A lookup or the hints of another piece are
// prefixed with that piece
std::string str(read_ahead::result const& r, lt::piece_index_t const p = piece)
{
	switch (r.what)
	{
		case read_ahead::action::none:
			return r.hints.empty() ? "none" : "none, with hints";
		case read_ahead::action::lookup:
			return r.piece == p ? "lookup" : "lookup " + std::to_string(static_cast<int>(r.piece));
		case read_ahead::action::hint:
			break;
	}
	std::string ret;
	for (auto const& h : r.hints)
	{
		if (!ret.empty())
			ret += ' ';
		if (h.piece != p)
			ret += std::to_string(static_cast<int>(h.piece)) + '/';
		ret += std::to_string(h.offset / kib) + '+' + std::to_string(h.length / kib);
	}
	return ret;
}

// a read of the 16 kiB block at `offset`
std::string read(read_ahead& ra,
	int const piece_size,
	int const offset,
	lt::aux::steady_clock::time_point const now,
	lt::piece_index_t const p = piece)
{
	return str(ra.on_read(st, p, piece_size, offset, 16 * kib, now), p);
}

// a read of three blocks (coalesced requests), which is read ahead of wherever
// it is in the piece
std::string read3(read_ahead& ra,
	int const piece_size,
	int const offset,
	lt::aux::steady_clock::time_point const now,
	lt::piece_index_t const p = piece)
{
	return str(ra.on_read(st, p, piece_size, offset, 48 * kib, now), p);
}

// add the piece, and read its first two blocks: two blocks in a row hint
// nothing, the block after them is read ahead of
void add_read(read_ahead& ra,
	lt::piece_index_t const p,
	int const piece_size,
	std::int64_t const limit,
	lt::aux::steady_clock::time_point const now,
	lt::storage_index_t const s = st)
{
	ra.add_piece(s, p, piece_size, limit, now);
	TEST_EQUAL(str(ra.on_read(s, p, piece_size, 0, 16 * kib, now), p), "none");
	TEST_EQUAL(str(ra.on_read(s, p, piece_size, 16 * kib, 16 * kib, now), p), "none");
}

}

TORRENT_TEST(read_ahead_lookup)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "lookup");
	TEST_EQUAL(ra.num_pieces(), 0);
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(ra.num_pieces(), 1);
	// known now. A single block: nothing hinted
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "none");
	// two blocks in a row either
	TEST_EQUAL(read(ra, 4 * mib, 16 * kib, now), "none");
	// the third makes a run of three: the read's window and the next one
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now), "0+1024 1024+1024");
	// added already: nothing changes
	ra.add_piece(st, piece, 4 * mib, 128 * kib, now);
	TEST_EQUAL(ra.num_pieces(), 1);
	TEST_EQUAL(read(ra, 4 * mib, 48 * kib, now), "none");
}

TORRENT_TEST(read_ahead_slides)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	add_read(ra, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now), "0+1024 1024+1024");
	// read through: entering a window hints the one after it, and the last
	// window has none after it
	for (int offset = 48 * kib; offset < 4 * mib; offset += 16 * kib)
	{
		std::string expect = "none";
		if (offset == 1 * mib)
			expect = "2048+1024";
		if (offset == 2 * mib)
			expect = "3072+1024";
		TEST_EQUAL(read(ra, 4 * mib, offset, now), expect);
	}
}

TORRENT_TEST(read_ahead_mid_window)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	add_read(ra, piece, 4 * mib, 1 * mib, now);
	// from the start of the window: reads of it before this one may not have
	// been submitted yet
	TEST_EQUAL(read3(ra, 4 * mib, 1 * mib + 512 * kib, now), "1024+1024 2048+1024");
	// a read before that is a reader of its own
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now), "0+1024");
}

TORRENT_TEST(read_ahead_ahead)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra0(0, ttl);
	add_read(ra0, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra0, 4 * mib, 32 * kib, now), "0+1024");
	TEST_EQUAL(read3(ra0, 4 * mib, 1 * mib, now), "1024+1024");

	read_ahead ra2(2, ttl);
	add_read(ra2, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra2, 4 * mib, 32 * kib, now), "0+1024 1024+1024 2048+1024");
	TEST_EQUAL(read3(ra2, 4 * mib, 1 * mib, now), "3072+1024");
}

TORRENT_TEST(read_ahead_window_size)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	lt::piece_index_t const p1{1};
	lt::piece_index_t const p2{2};
	lt::piece_index_t const p3{3};

	// at most 64 windows: a 64 MiB piece has 1 MiB ones
	add_read(ra, piece, 64 * mib, 128 * kib, now);
	TEST_EQUAL(read(ra, 64 * mib, 32 * kib, now), "0+1024 1024+1024");
	TEST_EQUAL(read3(ra, 64 * mib, 62 * mib, now), "63488+1024 64512+1024");

	// limit unknown: 1 MiB
	add_read(ra, p1, 4 * mib, 0, now);
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now, p1), "0+1024 1024+1024");

	// larger than the piece: one window, the whole piece
	add_read(ra, p2, 512 * kib, 8 * mib, now);
	TEST_EQUAL(read3(ra, 512 * kib, 256 * kib, now, p2), "0+512");

	// whole blocks
	add_read(ra, p3, 1 * mib, 4 * kib, now);
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now, p3), "32+16 48+16");
}

TORRENT_TEST(read_ahead_short_piece)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	// the last piece of a torrent: 17 blocks, so the last window is one block
	int const size = 272 * kib;
	add_read(ra, piece, size, 256 * kib, now);
	TEST_EQUAL(read(ra, size, 224 * kib, now), "none");
	TEST_EQUAL(read(ra, size, 240 * kib, now), "none");
	// its read reads it all: nothing to hint
	TEST_EQUAL(read(ra, size, 256 * kib, now), "none");
	TEST_EQUAL(read(ra, size, 32 * kib, now), "0+256");
	TEST_EQUAL(read(ra, size, 48 * kib, now), "none");

	// a piece of one block is never hinted, though its read is all of it
	lt::piece_index_t const p1{1};
	ra.add_piece(st, p1, 16 * kib, 1 * mib, now);
	TEST_EQUAL(read(ra, 16 * kib, 0, now, p1), "none");
}

TORRENT_TEST(read_ahead_span)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	ra.add_piece(st, piece, 1 * mib, 32 * kib, now);
	// a read across two windows has both, and the one after them, hinted,
	// the piece's first read too: it is three blocks
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, 16 * kib, 48 * kib, now)), "0+32 32+32 64+32");
	// read again: hinted already
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, 16 * kib, 48 * kib, now)), "none");
	// reads past the end of the piece are clipped to it
	TEST_EQUAL(
		str(ra.on_read(st, piece, 1 * mib, 1 * mib - 48 * kib, 64 * kib, now)), "960+32 992+32");
	// and outside of it, ignored
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, 1 * mib, 16 * kib, now)), "none");
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, -16 * kib, 16 * kib, now)), "none");
}

TORRENT_TEST(read_ahead_not_hinted)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	// the storage found no file to hint
	ra.add_piece(st, piece, 4 * mib, -1, now);
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "none");
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib, now), "none");
	TEST_EQUAL(ra.num_pieces(), 1);
}

TORRENT_TEST(read_ahead_scattered_reads)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	// single blocks far apart: nothing hinted, however many
	TEST_EQUAL(read(ra, 4 * mib, 2 * mib + 48 * kib, now), "none");
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib, now), "none");
	TEST_EQUAL(read(ra, 4 * mib, 3 * mib, now), "none");
	// a block next to one read before: two in a row, not enough either
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib + 16 * kib, now), "none");
	// a third makes a run of three: read ahead of, from its window
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib + 32 * kib, now), "1024+1024 2048+1024");
	// the other scattered reads left nothing hinted: a run from one of them
	// goes on from there
	TEST_EQUAL(read(ra, 4 * mib, 2 * mib + 64 * kib, now), "none");
	TEST_EQUAL(read(ra, 4 * mib, 2 * mib + 80 * kib, now), "3072+1024");

	// blocks just before ones read earlier make a run too: the disk threads
	// may take a peer's requests out of order
	lt::piece_index_t const p1{1};
	ra.add_piece(st, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 128 * kib, now, p1), "none");
	TEST_EQUAL(read(ra, 4 * mib, 112 * kib, now, p1), "none");
	TEST_EQUAL(read(ra, 4 * mib, 96 * kib, now, p1), "0+1024 1024+1024");

	// a read of three blocks or more (coalesced requests): read ahead of, the
	// piece's first read too
	lt::piece_index_t const p2{2};
	ra.add_piece(st, p2, 4 * mib, 1 * mib, now);
	TEST_EQUAL(str(ra.on_read(st, p2, 4 * mib, 0, 64 * kib, now), p2), "0+1024 1024+1024");

	// a read of two blocks: not on its own, but it makes a run of three with
	// the block after it
	lt::piece_index_t const p3{3};
	ra.add_piece(st, p3, 4 * mib, 1 * mib, now);
	TEST_EQUAL(str(ra.on_read(st, p3, 4 * mib, 0, 32 * kib, now), p3), "none");
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now, p3), "0+1024 1024+1024");

	// the last four runs are kept: a fifth replaces the oldest, which is
	// started over
	lt::piece_index_t const p4{4};
	ra.add_piece(st, p4, 4 * mib, 1 * mib, now);
	for (int const offset : {0, 1 * mib, 2 * mib, 3 * mib, 3 * mib + 512 * kib})
		TEST_EQUAL(read(ra, 4 * mib, offset, now, p4), "none");
	TEST_EQUAL(read(ra, 4 * mib, 16 * kib, now, p4), "none");
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now, p4), "none");
	TEST_EQUAL(read(ra, 4 * mib, 48 * kib, now, p4), "0+1024 1024+1024");
}

TORRENT_TEST(read_ahead_one_block_per_piece)
{
	// a peer that requests one block of every piece, in order: the pieces are
	// requested after one another, and none is read ahead of
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(2, ttl);
	for (int i = 0; i < 8; ++i)
		ra.on_request(st, lt::piece_index_t{i}, 0, now);
	for (int i = 0; i < 8; ++i)
	{
		lt::piece_index_t const p{i};
		TEST_EQUAL(read(ra, 4 * mib, 0, now, p), "lookup");
		ra.add_piece(st, p, 4 * mib, 1 * mib, now);
		TEST_EQUAL(read(ra, 4 * mib, 0, now, p), "none");
	}
	TEST_EQUAL(ra.num_pieces(), 8);
}

TORRENT_TEST(read_ahead_two_blocks_per_piece)
{
	// a peer that requests two blocks of every piece: none is read ahead of,
	// whether the blocks are far apart or next to each other, read one after
	// the other or together
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(2, ttl);
	for (int i = 0; i < 24; ++i)
		ra.on_request(st, lt::piece_index_t{i}, 0, now);
	for (int i = 0; i < 8; ++i)
	{
		lt::piece_index_t const p{i};
		lt::piece_index_t const next{i + 8};
		lt::piece_index_t const together{i + 16};
		for (lt::piece_index_t const q : {p, next, together})
			ra.add_piece(st, q, 4 * mib, 1 * mib, now);
		TEST_EQUAL(read(ra, 4 * mib, 0, now, p), "none");
		TEST_EQUAL(read(ra, 4 * mib, 2 * mib + 160 * kib, now, p), "none");
		TEST_EQUAL(read(ra, 4 * mib, 1 * mib, now, next), "none");
		TEST_EQUAL(read(ra, 4 * mib, 1 * mib + 16 * kib, now, next), "none");
		TEST_EQUAL(
			str(ra.on_read(st, together, 4 * mib, 1 * mib, 32 * kib, now), together), "none");
	}
}

TORRENT_TEST(read_ahead_ttl)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	lt::piece_index_t const p1{1};
	lt::piece_index_t const p2{2};
	add_read(ra, piece, 4 * mib, 1 * mib, now);
	add_read(ra, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now), "0+1024 1024+1024");
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now, p1), "0+1024 1024+1024");

	// read again within the ttl: hinted already
	TEST_EQUAL(read(ra, 4 * mib, 48 * kib, now + lt::milliseconds(900)), "none");

	// the pieces not read for the ttl are forgotten, at most once per ttl
	TEST_EQUAL(read(ra, 4 * mib, 0, now + lt::milliseconds(1000), p2), "lookup");
	TEST_EQUAL(ra.num_pieces(), 1);
	TEST_EQUAL(read(ra, 4 * mib, 0, now + lt::milliseconds(1000), p1), "lookup");

	// in between, a piece not read for the ttl starts over, its runs too: a
	// new run of three hints again
	TEST_EQUAL(read(ra, 4 * mib, 64 * kib, now + lt::milliseconds(1950)), "none");
	TEST_EQUAL(read(ra, 4 * mib, 80 * kib, now + lt::milliseconds(1950)), "none");
	TEST_EQUAL(read(ra, 4 * mib, 96 * kib, now + lt::milliseconds(1950)), "0+1024 1024+1024");
	TEST_EQUAL(ra.num_pieces(), 1);
}

TORRENT_TEST(read_ahead_remove_storage)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	lt::storage_index_t const st1{1};
	add_read(ra, piece, 4 * mib, 1 * mib, now);
	add_read(ra, piece, 4 * mib, 1 * mib, now, st1);
	TEST_EQUAL(str(ra.on_read(st1, piece, 4 * mib, 32 * kib, 16 * kib, now)), "0+1024 1024+1024");
	ra.remove_storage(st1);
	TEST_EQUAL(ra.num_pieces(), 1);
	TEST_EQUAL(str(ra.on_read(st1, piece, 4 * mib, 32 * kib, 16 * kib, now)), "lookup");
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now), "0+1024 1024+1024");
}

TORRENT_TEST(read_ahead_max_pieces)
{
	auto const t0 = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl, 4);
	for (int i = 0; i < 4; ++i)
		ra.add_piece(st, lt::piece_index_t{i}, 4 * mib, 1 * mib, t0 + lt::milliseconds(i));
	TEST_EQUAL(ra.num_pieces(), 4);
	// one more: the pieces used longest ago go, down to three quarters
	ra.add_piece(st, lt::piece_index_t{4}, 4 * mib, 1 * mib, t0 + lt::milliseconds(4));
	TEST_EQUAL(ra.num_pieces(), 3);
	TEST_EQUAL(read(ra, 4 * mib, 0, t0 + lt::milliseconds(5), lt::piece_index_t{0}), "lookup");
	TEST_EQUAL(read(ra, 4 * mib, 0, t0 + lt::milliseconds(5), lt::piece_index_t{1}), "lookup");
	TEST_EQUAL(read(ra, 4 * mib, 0, t0 + lt::milliseconds(5), lt::piece_index_t{2}), "none");

	// requests count as uses, and leave the request order with their piece
	read_ahead rq(1, ttl, 4);
	for (int i = 0; i < 6; ++i)
		rq.on_request(st, lt::piece_index_t{i}, 0, t0 + lt::milliseconds(i));
	TEST_EQUAL(rq.num_pieces(), 4);
	TEST_EQUAL(rq.num_requested(), 4);

	// never the pieces of the call: those used at the same time stay
	read_ahead r1(1, ttl, 1);
	r1.add_piece(st, lt::piece_index_t{0}, 4 * mib, 1 * mib, t0);
	r1.add_piece(st, lt::piece_index_t{1}, 4 * mib, 1 * mib, t0);
	TEST_EQUAL(r1.num_pieces(), 2);
	r1.add_piece(st, lt::piece_index_t{2}, 4 * mib, 1 * mib, t0 + lt::milliseconds(1));
	TEST_EQUAL(r1.num_pieces(), 1);
	TEST_EQUAL(read(r1, 4 * mib, 0, t0 + lt::milliseconds(1), lt::piece_index_t{2}), "none");
}

namespace {

lt::piece_index_t const p1{1};
lt::piece_index_t const p2{2};
lt::piece_index_t const p3{3};

// the blocks of a piece requested in order
void request(read_ahead& ra,
	lt::piece_index_t const p,
	int const piece_size,
	lt::aux::steady_clock::time_point const now)
{
	for (int offset = 0; offset < piece_size; offset += 16 * kib)
		ra.on_request(st, p, offset, now);
}

}

TORRENT_TEST(read_ahead_request_order)
{
	read_ahead ra(2, ttl);
	auto const now = lt::aux::steady_clock::now();
	for (lt::piece_index_t const p : {piece, p1, p2, p3})
		request(ra, p, 1 * mib, now);
	TEST_EQUAL(ra.num_requested(), 4);

	TEST_EQUAL(read(ra, 1 * mib, 0, now), "lookup");
	ra.add_piece(st, piece, 1 * mib, 1 * mib, now);
	// blocks that aren't a run of three need no other piece
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "none");
	TEST_EQUAL(read(ra, 1 * mib, 16 * kib, now), "none");
	// the pieces in reach of the third block are known first
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now), "lookup 1");
	ra.add_piece(st, p1, 1 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now), "lookup 2");
	ra.add_piece(st, p2, 1 * mib, 1 * mib, now);

	// a piece of one window has the next two pieces' windows in reach
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now), "0+1024 1/0+1024 2/0+1024");
	TEST_EQUAL(read(ra, 1 * mib, 48 * kib, now), "none");

	// the next piece's reads reach one piece further, from its third block
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p1), "none");
	TEST_EQUAL(read(ra, 1 * mib, 16 * kib, now, p1), "none");
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now, p1), "lookup 3");
	ra.add_piece(st, p3, 1 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now, p1), "3/0+1024");
	// and nothing was requested after the last one
	for (lt::piece_index_t const p : {p2, p3})
	{
		for (int offset = 0; offset < 64 * kib; offset += 16 * kib)
			TEST_EQUAL(read(ra, 1 * mib, offset, now, p), "none");
	}
}

TORRENT_TEST(read_ahead_request_order_windows)
{
	read_ahead ra(2, ttl);
	auto const now = lt::aux::steady_clock::now();
	request(ra, piece, 4 * mib, now);
	request(ra, p1, 4 * mib, now);
	add_read(ra, piece, 4 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now), "0+1024 1024+1024 2048+1024");
	// read through: the windows in reach past the end of the piece are the
	// next piece's
	for (int offset = 48 * kib; offset < 4 * mib; offset += 16 * kib)
	{
		std::string expect = "none";
		if (offset == 1 * mib)
			expect = "3072+1024";
		if (offset == 2 * mib)
			expect = "1/0+1024";
		if (offset == 3 * mib)
			expect = "1/1024+1024";
		TEST_EQUAL(read(ra, 4 * mib, offset, now), expect);
	}
	// which carries on from there, from its third block
	TEST_EQUAL(read(ra, 4 * mib, 0, now, p1), "none");
	TEST_EQUAL(read(ra, 4 * mib, 16 * kib, now, p1), "none");
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now, p1), "2048+1024");
}

TORRENT_TEST(read_ahead_request_order_offset)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.on_request(st, piece, 0, now);
	// the next piece is requested from its third window first
	ra.on_request(st, p1, 2 * mib, now);
	ra.on_request(st, p1, 0, now);
	add_read(ra, piece, 1 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now), "0+1024 1/2048+1024");
}

TORRENT_TEST(read_ahead_request_order_storage)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	lt::storage_index_t const st1{1};
	ra.on_request(st, piece, 0, now);
	ra.on_request(st1, p1, 0, now);
	// requested already: its place stays
	ra.on_request(st, piece, 16 * kib, now);
	ra.on_request(st, p2, 0, now);
	TEST_EQUAL(ra.num_requested(), 3);
	add_read(ra, piece, 1 * mib, 1 * mib, now);
	add_read(ra, p1, 1 * mib, 1 * mib, now, st1);
	ra.add_piece(st, p2, 1 * mib, 1 * mib, now);
	// the next piece of the same storage
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now), "0+1024 2/0+1024");
	TEST_EQUAL(str(ra.on_read(st1, p1, 1 * mib, 32 * kib, 16 * kib, now), p1), "0+1024");

	// forgotten with the storage
	ra.remove_storage(st);
	TEST_EQUAL(ra.num_requested(), 1);
	TEST_EQUAL(ra.num_pieces(), 1);
}

TORRENT_TEST(read_ahead_request_order_not_hinted)
{
	read_ahead ra(2, ttl);
	auto const now = lt::aux::steady_clock::now();
	for (lt::piece_index_t const p : {piece, p1, p2})
		request(ra, p, 1 * mib, now);
	add_read(ra, piece, 1 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 1 * mib, -1, now);
	ra.add_piece(st, p2, 1 * mib, 1 * mib, now);
	// a piece that isn't hinted ends the ones in reach
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, now), "0+1024");
}

TORRENT_TEST(read_ahead_request_order_read_anyway)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.on_request(st, piece, 0, now);
	ra.on_request(st, p1, 0, now);
	ra.add_piece(st, piece, 32 * kib, 16 * kib, now);
	ra.add_piece(st, p1, 32 * kib, 16 * kib, now);
	// the read covers its piece, a run of all of it though shorter than three
	// blocks: only the next piece is hinted
	TEST_EQUAL(str(ra.on_read(st, piece, 32 * kib, 0, 32 * kib, now)), "1/0+16");
	TEST_EQUAL(str(ra.on_read(st, piece, 32 * kib, 0, 32 * kib, now)), "none");
}

TORRENT_TEST(read_ahead_request_ttl)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.on_request(st, piece, 0, now);
	ra.on_request(st, p1, 0, now);
	// requested again within the ttl: its place stays
	auto const t1 = now + lt::milliseconds(500);
	ra.on_request(st, piece, 0, t1);
	for (lt::piece_index_t const p : {piece, p1, p2})
		ra.add_piece(st, p, 1 * mib, 1 * mib, t1);
	TEST_EQUAL(read(ra, 1 * mib, 0, t1), "none");
	TEST_EQUAL(read(ra, 1 * mib, 16 * kib, t1), "none");
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, t1), "0+1024 1/0+1024");

	// requested again later than that: it moves to the end
	auto const t2 = now + lt::milliseconds(1100);
	ra.on_request(st, p2, 0, t2);
	ra.on_request(st, piece, 0, t2);
	TEST_EQUAL(ra.num_requested(), 3);
	TEST_EQUAL(read(ra, 1 * mib, 0, t2), "none");
	TEST_EQUAL(read(ra, 1 * mib, 0, t2, p1), "none");
	TEST_EQUAL(read(ra, 1 * mib, 16 * kib, t2, p1), "none");
	TEST_EQUAL(read(ra, 1 * mib, 32 * kib, t2, p1), "2/0+1024");

	// neither read nor requested for the ttl: forgotten
	auto const t3 = t2 + ttl;
	TEST_EQUAL(read(ra, 1 * mib, 0, t3), "lookup");
	TEST_EQUAL(ra.num_pieces(), 0);
	TEST_EQUAL(ra.num_requested(), 0);
}
