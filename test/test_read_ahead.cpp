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

}

TORRENT_TEST(read_ahead_lookup)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "lookup");
	TEST_EQUAL(ra.num_pieces(), 0);
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(ra.num_pieces(), 1);
	// known now: the read's window and the next one
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "0+1024 1024+1024");
	// added already: nothing changes
	ra.add_piece(st, piece, 4 * mib, 128 * kib, now);
	TEST_EQUAL(ra.num_pieces(), 1);
	TEST_EQUAL(read(ra, 4 * mib, 16 * kib, now), "none");
}

TORRENT_TEST(read_ahead_slides)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "0+1024 1024+1024");
	for (int offset = 16 * kib; offset < 1 * mib; offset += 16 * kib)
		TEST_EQUAL(read(ra, 4 * mib, offset, now), "none");
	// entering a window hints the one after it
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib, now), "2048+1024");
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib + 16 * kib, now), "none");
	TEST_EQUAL(read(ra, 4 * mib, 2 * mib, now), "3072+1024");
	// the last window has none after it
	TEST_EQUAL(read(ra, 4 * mib, 3 * mib, now), "none");
}

TORRENT_TEST(read_ahead_mid_window)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	// from the start of the window: reads of it before this one may not have
	// been submitted yet
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib + 512 * kib, now), "1024+1024 2048+1024");
	// a read before that is a reader of its own
	TEST_EQUAL(read(ra, 4 * mib, 16 * kib, now), "0+1024");
}

TORRENT_TEST(read_ahead_ahead)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra0(0, ttl);
	ra0.add_piece(st, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra0, 4 * mib, 0, now), "0+1024");
	TEST_EQUAL(read(ra0, 4 * mib, 1 * mib, now), "1024+1024");

	read_ahead ra2(2, ttl);
	ra2.add_piece(st, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra2, 4 * mib, 0, now), "0+1024 1024+1024 2048+1024");
	TEST_EQUAL(read(ra2, 4 * mib, 1 * mib, now), "3072+1024");
}

TORRENT_TEST(read_ahead_window_size)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	lt::piece_index_t const p1{1};
	lt::piece_index_t const p2{2};
	lt::piece_index_t const p3{3};

	// at most 64 windows: a 64 MiB piece has 1 MiB ones
	ra.add_piece(st, piece, 64 * mib, 128 * kib, now);
	TEST_EQUAL(read(ra, 64 * mib, 0, now), "0+1024 1024+1024");
	TEST_EQUAL(read(ra, 64 * mib, 62 * mib, now), "63488+1024 64512+1024");

	// limit unknown: 128 kiB
	ra.add_piece(st, p1, 1 * mib, 0, now);
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p1), "0+128 128+128");

	// larger than the piece: one window, the whole piece
	ra.add_piece(st, p2, 512 * kib, 8 * mib, now);
	TEST_EQUAL(read(ra, 512 * kib, 256 * kib, now, p2), "0+512");

	// whole blocks
	ra.add_piece(st, p3, 1 * mib, 4 * kib, now);
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p3), "0+16 16+16");
}

TORRENT_TEST(read_ahead_short_piece)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	// the last piece of a torrent: 17 blocks, so the last window is one block
	int const size = 272 * kib;
	ra.add_piece(st, piece, size, 256 * kib, now);
	// its read reads it all: nothing to hint
	TEST_EQUAL(read(ra, size, 256 * kib, now), "none");
	TEST_EQUAL(read(ra, size, 0, now), "0+256");
	TEST_EQUAL(read(ra, size, 16 * kib, now), "none");

	// a piece of one block is never hinted
	lt::piece_index_t const p1{1};
	ra.add_piece(st, p1, 16 * kib, 1 * mib, now);
	TEST_EQUAL(read(ra, 16 * kib, 0, now, p1), "none");
}

TORRENT_TEST(read_ahead_span)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	ra.add_piece(st, piece, 1 * mib, 32 * kib, now);
	// a read across two windows has both, and the one after them, hinted
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, 16 * kib, 32 * kib, now)), "0+32 32+32 64+32");
	// read again: hinted already
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, 16 * kib, 32 * kib, now)), "none");
	// reads past the end of the piece are clipped to it
	TEST_EQUAL(str(ra.on_read(st, piece, 1 * mib, 1 * mib - 16 * kib, 32 * kib, now)), "992+32");
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

TORRENT_TEST(read_ahead_ttl)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	lt::piece_index_t const p1{1};
	lt::piece_index_t const p2{2};
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "0+1024 1024+1024");
	TEST_EQUAL(read(ra, 4 * mib, 0, now, p1), "0+1024 1024+1024");

	// read again within the ttl: hinted already
	TEST_EQUAL(read(ra, 4 * mib, 16 * kib, now + lt::milliseconds(900)), "none");

	// the pieces not read for the ttl are forgotten, at most once per ttl
	TEST_EQUAL(read(ra, 4 * mib, 0, now + lt::milliseconds(1000), p2), "lookup");
	TEST_EQUAL(ra.num_pieces(), 1);
	TEST_EQUAL(read(ra, 4 * mib, 0, now + lt::milliseconds(1000), p1), "lookup");

	// in between, a piece not read for the ttl is hinted again
	TEST_EQUAL(read(ra, 4 * mib, 32 * kib, now + lt::milliseconds(1950)), "0+1024 1024+1024");
	TEST_EQUAL(ra.num_pieces(), 1);
}

TORRENT_TEST(read_ahead_remove_storage)
{
	auto const now = lt::aux::steady_clock::now();
	read_ahead ra(1, ttl);
	lt::storage_index_t const st1{1};
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	ra.add_piece(st1, piece, 4 * mib, 1 * mib, now);
	TEST_EQUAL(str(ra.on_read(st1, piece, 4 * mib, 0, 16 * kib, now)), "0+1024 1024+1024");
	ra.remove_storage(st1);
	TEST_EQUAL(ra.num_pieces(), 1);
	TEST_EQUAL(str(ra.on_read(st1, piece, 4 * mib, 0, 16 * kib, now)), "lookup");
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "0+1024 1024+1024");
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

	// the pieces in reach are known first
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "lookup");
	ra.add_piece(st, piece, 1 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "lookup 1");
	ra.add_piece(st, p1, 1 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "lookup 2");
	ra.add_piece(st, p2, 1 * mib, 1 * mib, now);

	// a piece of one window has the next two pieces' windows in reach
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "0+1024 1/0+1024 2/0+1024");
	TEST_EQUAL(read(ra, 1 * mib, 16 * kib, now), "none");

	// the next piece's read reaches one piece further
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p1), "lookup 3");
	ra.add_piece(st, p3, 1 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p1), "3/0+1024");
	// and nothing was requested after the last one
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p2), "none");
	TEST_EQUAL(read(ra, 1 * mib, 0, now, p3), "none");
}

TORRENT_TEST(read_ahead_request_order_windows)
{
	read_ahead ra(2, ttl);
	auto const now = lt::aux::steady_clock::now();
	request(ra, piece, 4 * mib, now);
	request(ra, p1, 4 * mib, now);
	ra.add_piece(st, piece, 4 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 4 * mib, 0, now), "0+1024 1024+1024 2048+1024");
	TEST_EQUAL(read(ra, 4 * mib, 1 * mib, now), "3072+1024");
	// the windows in reach past the end of the piece are the next piece's
	TEST_EQUAL(read(ra, 4 * mib, 2 * mib, now), "1/0+1024");
	TEST_EQUAL(read(ra, 4 * mib, 3 * mib, now), "1/1024+1024");
	// which carries on from there
	TEST_EQUAL(read(ra, 4 * mib, 0, now, p1), "2048+1024");
}

TORRENT_TEST(read_ahead_request_order_offset)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.on_request(st, piece, 0, now);
	// the next piece is requested from its third window first
	ra.on_request(st, p1, 2 * mib, now);
	ra.on_request(st, p1, 0, now);
	ra.add_piece(st, piece, 1 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 4 * mib, 1 * mib, now);
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "0+1024 1/2048+1024");
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
	ra.add_piece(st, piece, 1 * mib, 1 * mib, now);
	ra.add_piece(st1, p1, 1 * mib, 1 * mib, now);
	ra.add_piece(st, p2, 1 * mib, 1 * mib, now);
	// the next piece of the same storage
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "0+1024 2/0+1024");
	TEST_EQUAL(str(ra.on_read(st1, p1, 1 * mib, 0, 16 * kib, now), p1), "0+1024");

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
	ra.add_piece(st, piece, 1 * mib, 1 * mib, now);
	ra.add_piece(st, p1, 1 * mib, -1, now);
	ra.add_piece(st, p2, 1 * mib, 1 * mib, now);
	// a piece that isn't hinted ends the ones in reach
	TEST_EQUAL(read(ra, 1 * mib, 0, now), "0+1024");
}

TORRENT_TEST(read_ahead_request_order_read_anyway)
{
	read_ahead ra(1, ttl);
	auto const now = lt::aux::steady_clock::now();
	ra.on_request(st, piece, 0, now);
	ra.on_request(st, p1, 0, now);
	ra.add_piece(st, piece, 32 * kib, 16 * kib, now);
	ra.add_piece(st, p1, 32 * kib, 16 * kib, now);
	// the read covers its piece: only the next piece is hinted
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
	TEST_EQUAL(read(ra, 1 * mib, 0, t1), "0+1024 1/0+1024");

	// requested again later than that: it moves to the end
	auto const t2 = now + lt::milliseconds(1100);
	ra.on_request(st, p2, 0, t2);
	ra.on_request(st, piece, 0, t2);
	TEST_EQUAL(ra.num_requested(), 3);
	TEST_EQUAL(read(ra, 1 * mib, 0, t2), "none");
	TEST_EQUAL(read(ra, 1 * mib, 0, t2, p1), "2/0+1024");

	// neither read nor requested for the ttl: forgotten
	auto const t3 = t2 + ttl;
	TEST_EQUAL(read(ra, 1 * mib, 0, t3), "lookup");
	TEST_EQUAL(ra.num_pieces(), 0);
	TEST_EQUAL(ra.num_requested(), 0);
}
