/*

Copyright (c) 2008, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/units.hpp"

#include <memory>
#include <functional>
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <iostream>

#include "test.hpp"
#include "test_utils.hpp"

using namespace lt;
using namespace std::placeholders;

namespace {

const int blocks_per_piece = 4;

typed_bitfield<piece_index_t> string2vec(char const* have_str)
{
	const int num_pieces = int(strlen(have_str));
	typed_bitfield<piece_index_t> have(num_pieces, false);
	for (piece_index_t i(0); i < have.end_index(); ++i)
		if (have_str[static_cast<int>(i)] != ' ') have.set_bit(i);
	return have;
}

tcp::endpoint endp;
ipv4_peer tmp0(endp, false, {});
ipv4_peer tmp1(endp, false, {});
ipv4_peer tmp2(endp, false, {});
ipv4_peer tmp3(endp, false, {});
ipv4_peer tmp4(endp, false, {});
ipv4_peer tmp5(endp, false, {});
ipv4_peer tmp6(endp, false, {});
ipv4_peer tmp7(endp, false, {});
ipv4_peer tmp8(endp, false, {});
ipv4_peer tmp9(endp, false, {});
ipv4_peer peer_struct(endp, true, {});
ipv4_peer* tmp_peer = &tmp1;

static std::vector<piece_index_t> const empty_vector;

#if TORRENT_USE_ASSERTS
namespace { // TODO: remove the nested namespace
	static struct initializer
	{
		initializer()
		{
			tmp0.in_use = true;
			tmp1.in_use = true;
			tmp2.in_use = true;
			tmp3.in_use = true;
			tmp4.in_use = true;
			tmp5.in_use = true;
			tmp6.in_use = true;
			tmp7.in_use = true;
			tmp8.in_use = true;
			tmp9.in_use = true;
			peer_struct.in_use = true;
		}

	} initializer_dummy;
}
#endif

// availability is a string where each character is the
// availability of that piece, '1', '2' etc.
// have_str is a string where each character represents a
// piece, ' ' means we don't have the piece and any other
// character means we have it
std::shared_ptr<piece_picker> setup_picker(
	char const* availability
	, char const* have_str
	, char const* priority
	, char const* partial
	, int num_blocks_per_piece = blocks_per_piece)
{
	const int num_pieces = int(strlen(availability));
	TORRENT_ASSERT(int(strlen(have_str)) == num_pieces);

	std::shared_ptr<piece_picker> p = std::make_shared<piece_picker>(num_blocks_per_piece, num_blocks_per_piece, num_pieces);

	for (piece_index_t i(0); i < piece_index_t(num_pieces); ++i)
	{
		const int avail = availability[static_cast<int>(i)] - '0';
		assert(avail >= 0);

		static const torrent_peer* peers[10] = { &tmp0, &tmp1, &tmp2
			, &tmp3, &tmp4, &tmp5, &tmp6, &tmp7, &tmp8, &tmp9 };
		TORRENT_ASSERT(avail < 10);
		for (int j = 0; j < avail; ++j) p->inc_refcount(i, peers[j]);
	}

	auto have = string2vec(have_str);

	for (piece_index_t i(0); i < have.end_index(); ++i)
	{
		int const idx = static_cast<int>(i);
		if (partial[idx] == 0) break;

		if (partial[idx] == ' ') continue;

		int blocks = 0;
		if (partial[idx] >= '0' && partial[idx] <= '9')
			blocks = partial[idx] - '0';
		else
			blocks = partial[idx] - 'a' + 10;

		int counter = 0;
		for (int j = 0; j < 4; ++j)
		{
			TEST_CHECK(!p->is_finished(piece_block(i, j)));
			if ((blocks & (1 << j)) == 0) continue;
			++counter;
			bool ret = p->mark_as_downloading(piece_block(i, j), tmp_peer);
			TEST_CHECK(ret == true);
			TEST_CHECK(p->is_requested(piece_block(i, j)) == ((blocks & (1 << j)) != 0));
			p->mark_as_writing(piece_block(i, j), tmp_peer);
			TEST_CHECK(!p->is_finished(piece_block(i, j)));
			// trying to mark a block as requested after it has been completed
			// should fail (return false)
			ret = p->mark_as_downloading(piece_block(i, j), tmp_peer);
			TEST_CHECK(ret == false);
			p->mark_as_finished(piece_block(i, j), tmp_peer);

			TEST_CHECK(p->is_downloaded(piece_block(i, j)) == ((blocks & (1 << j)) != 0));
			TEST_CHECK(p->is_finished(piece_block(i, j)) == ((blocks & (1 << j)) != 0));
		}

		piece_picker::downloading_piece st;
		p->piece_info(i, st);
		TEST_EQUAL(int(st.writing), 0);
		TEST_EQUAL(int(st.requested), 0);
		TEST_EQUAL(int(st.index), idx);

		TEST_EQUAL(st.finished, counter);
		TEST_EQUAL(st.finished + st.requested + st.writing, counter);

		TEST_CHECK(p->is_piece_finished(i) == (counter == 4));
	}

	for (piece_index_t i(0); i < piece_index_t(num_pieces); ++i)
	{
		int const idx = static_cast<int>(i);
		if (priority[idx] == 0) break;
		download_priority_t const prio((priority[idx] - '0') & 0xff);
		TEST_CHECK(prio >= dont_download);
		p->set_piece_priority(i, prio);

		TEST_CHECK(p->piece_priority(i) == prio);
	}

	for (piece_index_t i(0); i < piece_index_t(num_pieces); ++i)
	{
		if (!have[i]) continue;
		p->we_have(i);
		for (int j = 0; j < num_blocks_per_piece; ++j)
			TEST_CHECK(p->is_finished(piece_block(i, j)));
	}

	aux::vector<int, piece_index_t> availability_vec;
	p->get_availability(availability_vec);
	for (piece_index_t i(0); i < piece_index_t(num_pieces); ++i)
	{
		const int avail = availability[static_cast<int>(i)] - '0';
		assert(avail >= 0);
		TEST_CHECK(avail == availability_vec[i]);
	}

#if TORRENT_USE_INVARIANT_CHECKS
	p->check_invariant();
#endif
	return p;
}

bool verify_pick(std::shared_ptr<piece_picker> p
	, std::vector<piece_block> const& picked, bool allow_multi_blocks = false)
{
#if TORRENT_USE_INVARIANT_CHECKS
	p->check_invariant();
#endif
	if (!allow_multi_blocks)
	{
		for (std::vector<piece_block>::const_iterator i = picked.begin()
			, end(picked.end()); i != end; ++i)
		{
			if (p->num_peers(*i) > 0) return false;
		}
	}

	// make sure there are no duplicated
	std::set<piece_block> blocks;
	std::copy(picked.begin(), picked.end()
		, std::insert_iterator<std::set<piece_block>>(blocks, blocks.end()));
	std::cout << " verify: " << picked.size() << " " << blocks.size() << std::endl;
	return picked.size() == blocks.size();
}

void print_availability(std::shared_ptr<piece_picker> const& p)
{
	aux::vector<int, piece_index_t> avail;
	p->get_availability(avail);
	std::printf("[ ");
	for (auto i : avail)
		std::printf("%d ", i);
	std::printf("]\n");
}

bool verify_availability(std::shared_ptr<piece_picker> const& p, char const* a)
{
	aux::vector<int, piece_index_t> avail;
	p->get_availability(avail);
	for (auto i = avail.begin(), end(avail.end()); i != end; ++i, ++a)
	{
		if (*a - '0' != *i) return false;
	}
	return true;
}

void print_pick(std::vector<piece_block> const& picked)
{
	for (auto const& p : picked)
	{
		std::cout << "(" << p.piece_index << ", " << p.block_index << ") ";
	}
	std::cout << std::endl;
}

std::vector<piece_block> pick_pieces(std::shared_ptr<piece_picker> const& p
	, char const* availability
	, int num_blocks
	, int prefer_contiguous_blocks
	, torrent_peer* peer_struct_arg
	, picker_options_t const options = piece_picker::rarest_first
	, std::vector<piece_index_t> const& suggested_pieces = empty_vector)
{
	std::vector<piece_block> picked;
	counters pc;
	p->pick_pieces(string2vec(availability), picked
		, num_blocks, prefer_contiguous_blocks, peer_struct_arg
		, options, suggested_pieces, 20, pc);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	return picked;
}

piece_index_t test_pick(std::shared_ptr<piece_picker> const& p
	, picker_options_t const options = piece_picker::rarest_first)
{
	std::vector<piece_block> picked = pick_pieces(p, "*******", 1, 0, nullptr
		, options, empty_vector);
	if (picked.size() != 1) return piece_index_t(-1);
	return picked[0].piece_index;
}

picker_options_t const options = piece_picker::rarest_first;
counters pc;

} // anonymous namespace

TORRENT_TEST(piece_block)
{
	piece_index_t const zero(0);
	piece_index_t const one(1);

	TEST_CHECK(piece_block(zero, 0) != piece_block(zero, 1));
	TEST_CHECK(piece_block(zero, 0) != piece_block(one, 0));
	TEST_CHECK(!(piece_block(zero, 0) != piece_block(zero, 0)));

	TEST_CHECK(!(piece_block(zero, 0) == piece_block(zero, 1)));
	TEST_CHECK(!(piece_block(zero, 0) == piece_block(one, 0)));
	TEST_CHECK(piece_block(zero, 0) == piece_block(zero, 0));

	TEST_CHECK(!(piece_block(zero, 1) < piece_block(zero, 0)));
	TEST_CHECK(!(piece_block(one, 0) < piece_block(zero, 0)));
	TEST_CHECK(piece_block(zero, 0) < piece_block(zero, 1));
	TEST_CHECK(piece_block(zero, 0) < piece_block(one, 0));
	TEST_CHECK(!(piece_block(zero, 0) < piece_block(zero, 0)));
	TEST_CHECK(!(piece_block(one, 0) < piece_block(one, 0)));
	TEST_CHECK(!(piece_block(zero, 1) < piece_block(zero, 1)));
}

TORRENT_TEST(abort_download)
{
	// test abort_download
	auto p = setup_picker("1111111", "       ", "7110000", "");
	auto picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) != picked.end());

	p->abort_download({piece_index_t(0), 0}, tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) != picked.end());

	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == true);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) == picked.end());

	p->abort_download({piece_index_t(0), 0}, tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) != picked.end());

	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1);
	p->mark_as_downloading({piece_index_t(0), 1}, &tmp1);
	p->abort_download({piece_index_t(0), 0}, tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) != picked.end());

	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1);
	p->mark_as_writing({piece_index_t(0), 0}, &tmp1);
	p->write_failed({piece_index_t(0), 0});
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(1), 0)) != picked.end()
		|| std::find(picked.begin(), picked.end(), piece_block(piece_index_t(2), 0)) != picked.end());
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) == picked.end());
	p->restore_piece(piece_index_t(0));
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) != picked.end());

	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1);
	p->mark_as_writing({piece_index_t(0), 0}, &tmp1);
	p->mark_as_finished({piece_index_t(0), 0}, &tmp1);
	p->abort_download({piece_index_t(0), 0}, tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) == picked.end());
}

TORRENT_TEST(abort_download2)
{
	auto p = setup_picker("1111111", "       ", "7110000", "");
	piece_picker::downloading_piece st;
	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1);
	p->mark_as_finished({piece_index_t(0), 1}, nullptr);
	p->piece_info(piece_index_t(0), st);
	TEST_EQUAL(st.requested, 1);
	TEST_EQUAL(st.finished, 1);
	p->abort_download({piece_index_t(0), 0}, tmp_peer);
	p->piece_info(piece_index_t(0), st);
	TEST_EQUAL(st.requested, 0);
	TEST_EQUAL(st.finished, 1);
	auto picked = pick_pieces(p, "*******", blocks_per_piece, 0, nullptr
		, options, empty_vector);
	TEST_CHECK(p->is_requested({piece_index_t(0), 0}) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(0), 0)) != picked.end());
}

TORRENT_TEST(get_downloaders)
{
	auto p = setup_picker("1111111", "       ", "7110000", "");

	p->mark_as_downloading({piece_index_t(0), 2}, &tmp1);
	p->mark_as_writing({piece_index_t(0), 2}, &tmp1);
	p->abort_download({piece_index_t(0), 2}, &tmp1);
	p->mark_as_downloading({piece_index_t(0), 2}, &tmp2);
	p->mark_as_writing({piece_index_t(0), 2}, &tmp2);

	std::vector<torrent_peer*> d;
	p->get_downloaders(d, piece_index_t(0));
	TEST_EQUAL(d.size(), 4);
	TEST_CHECK(d[0] == nullptr);
	TEST_CHECK(d[1] == nullptr);
	TEST_CHECK(d[2] == &tmp2);
	TEST_CHECK(d[3] == nullptr);

	p->mark_as_downloading({piece_index_t(0), 3}, &tmp1);
	p->abort_download({piece_index_t(0), 3}, &tmp1);
	p->mark_as_downloading({piece_index_t(0), 3}, &tmp2);
	p->mark_as_writing({piece_index_t(0), 3}, &tmp2);

	p->get_downloaders(d, piece_index_t(0));

	TEST_EQUAL(d.size(), 4);
	TEST_CHECK(d[0] == nullptr);
	TEST_CHECK(d[1] == nullptr);
	TEST_CHECK(d[2] == &tmp2);
	TEST_CHECK(d[3] == &tmp2);

	// if we ask for downloaders for a piece that's not
	// curently being downloaded, we get zeroes back
	p->get_downloaders(d, piece_index_t(1));

	TEST_EQUAL(d.size(), 4);
	TEST_CHECK(d[0] == nullptr);
	TEST_CHECK(d[1] == nullptr);
	TEST_CHECK(d[2] == nullptr);
	TEST_CHECK(d[3] == nullptr);

// ========================================================

	p = setup_picker("2222", "    ", "", "");

	for (piece_index_t i(0); i < piece_index_t(4); ++i)
		for (int k = 0; k < blocks_per_piece; ++k)
			p->mark_as_downloading(piece_block(i, k), &tmp1);

	p->mark_as_downloading({piece_index_t(0), 0}, &tmp2);

	std::printf("num_peers: %d\n", p->num_peers({piece_index_t(0), 0}));
	TEST_EQUAL(p->num_peers({piece_index_t(0), 0}), 2);

	p->abort_download({piece_index_t(0), 0}, &tmp1);

	std::printf("num_peers: %d\n", p->num_peers({piece_index_t(0), 0}));
	TEST_EQUAL(p->num_peers({piece_index_t(0), 0}), 1);
}

TORRENT_TEST(pick_lowest_availability)
{
	// make sure the block that is picked is from piece 1, since it
	// it is the piece with the lowest availability
	auto p = setup_picker("2223333", "* * *  ", "", "");
	TEST_CHECK(test_pick(p) == piece_index_t(1));
}

TORRENT_TEST(random_pick_at_same_priority)
{
	// make sure pieces with equal priority and availability
	// are picked at random
	std::map<piece_index_t, int> random_prio_pieces;
	for (int i = 0; i < 100; ++i)
	{
		auto p = setup_picker("1111112", "       ", "", "");
		++random_prio_pieces[test_pick(p)];
	}
	TEST_CHECK(random_prio_pieces.size() == 6);
	for (auto i = random_prio_pieces.begin()
		, end(random_prio_pieces.end()); i != end; ++i)
		std::cout << static_cast<int>(i->first) << ": " << i->second << " ";
	std::cout << std::endl;
}

TORRENT_TEST(pick_highest_priority)
{
	// make sure the block that is picked is from piece 5, since it
	// has the highest priority among the available pieces
	auto p = setup_picker("1111111", "       ", "1111121", "");
	TEST_CHECK(test_pick(p) == piece_index_t(5));

	p = setup_picker("1111111", "       ", "1171121", "");
	TEST_CHECK(test_pick(p) == piece_index_t(2));

	p = setup_picker("1111111", "       ", "1131521", "");
	TEST_CHECK(test_pick(p) == piece_index_t(4));
}

TORRENT_TEST(reverse_rarest_first)
{
	auto p = setup_picker("4179253", "       ", "", "");
	auto picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, &peer_struct
		, piece_picker::rarest_first | piece_picker::reverse, empty_vector);
	int expected_common_pieces[] = {3, 2, 5, 0, 6, 4, 1};
	for (int i = 0; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(
			expected_common_pieces[i / blocks_per_piece])
			, i % blocks_per_piece));
	}

	// piece 3 should NOT be prioritized since it's a partial, and not
	// reversed. Reversed partials are considered reversed
	p = setup_picker("1122111", "       ", "3333333", "   1   ");
	TEST_CHECK(test_pick(p, piece_picker::rarest_first | piece_picker::reverse)
		== piece_index_t(2));
}

TORRENT_TEST(pick_whole_pieces)
{
	// make sure the 4 blocks are picked from the same piece if
	// whole pieces are preferred. Priority and availability is more
	// important. Piece 1 has the lowest availability even though
	// it is not a whole piece
	auto p = setup_picker("2212222", "       ", "1111111", "1023460");
	auto picked = pick_pieces(p, "****** ", 1, blocks_per_piece
		, &peer_struct, options, empty_vector);
	TEST_EQUAL(int(picked.size()), 3);
	for (int i = 0; i < blocks_per_piece && i < int(picked.size()); ++i)
		TEST_EQUAL(picked[std::size_t(i)].piece_index, piece_index_t(2));

	p = setup_picker("1111111", "       ", "1111111", "");
	picked = pick_pieces(p, "****** ", 1, blocks_per_piece
		, &peer_struct, options, empty_vector);
	TEST_EQUAL(int(picked.size()), blocks_per_piece);
	for (int i = 0; i < blocks_per_piece && i < int(picked.size()); ++i)
		TEST_EQUAL(picked[std::size_t(i)].block_index, i);

	p = setup_picker("2221222", "       ", "", "");
	picked = pick_pieces(p, "*******", 1, 7 * blocks_per_piece
		, &peer_struct, options, empty_vector);
	TEST_EQUAL(int(picked.size()), 7 * blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(i / blocks_per_piece)
			, i % blocks_per_piece));
}

TORRENT_TEST(distributed_copies)
{
	// test the distributed copies function. It should include ourself
	// in the availability. i.e. piece 0 has availability 2.
	// there are 2 pieces with availability 2 and 5 with availability 3
	auto p = setup_picker("1233333", "*      ", "", "");
	auto dc = p->distributed_copies();
	TEST_CHECK(dc == std::make_pair(2, 5000 / 7));
}

TORRENT_TEST(filtered_pieces)
{
	// make sure filtered pieces are ignored
	auto p = setup_picker("1111111", "       ", "0010000", "");
	TEST_CHECK(test_pick(p, piece_picker::rarest_first) == piece_index_t(2));
	TEST_CHECK(test_pick(p, piece_picker::rarest_first | piece_picker::reverse) == piece_index_t(2));
	TEST_CHECK(test_pick(p, piece_picker::sequential) == piece_index_t(2));
	TEST_CHECK(test_pick(p, piece_picker::sequential | piece_picker::reverse) == piece_index_t(2));
}

TORRENT_TEST(we_dont_have)
{
	// make sure we_dont_have works
	auto p = setup_picker("1111111", "*******", "0100000", "");
	TEST_CHECK(p->have_piece(piece_index_t(1)));
	TEST_CHECK(p->have_piece(piece_index_t(2)));
	p->we_dont_have(piece_index_t(1));
	p->we_dont_have(piece_index_t(2));
	TEST_CHECK(!p->have_piece(piece_index_t(1)));
	TEST_CHECK(!p->have_piece(piece_index_t(2)));
	auto picked = pick_pieces(p, "*** ** ", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == piece_index_t(1));
}

TORRENT_TEST(dec_refcount_split_seed)
{
	// make sure we can split m_seed when removing a refcount
	auto p = setup_picker("0000000", "       ", "0000000", "");
	p->inc_refcount_all(nullptr);

	aux::vector<int, piece_index_t> avail;
	p->get_availability(avail);
	TEST_EQUAL(avail.size(), 7);
	TEST_CHECK(avail[piece_index_t(0)] != 0);
	TEST_CHECK(avail[piece_index_t(1)] != 0);
	TEST_CHECK(avail[piece_index_t(2)] != 0);
	TEST_CHECK(avail[piece_index_t(3)] != 0);
	TEST_CHECK(avail[piece_index_t(4)] != 0);

	p->dec_refcount(piece_index_t(3), nullptr);

	p->get_availability(avail);
	TEST_EQUAL(avail.size(), 7);

	TEST_CHECK(avail[piece_index_t(0)] != 0);
	TEST_CHECK(avail[piece_index_t(1)] != 0);
	TEST_CHECK(avail[piece_index_t(2)] != 0);
	TEST_CHECK(avail[piece_index_t(3)] == 0);
	TEST_CHECK(avail[piece_index_t(4)] != 0);
}

TORRENT_TEST(resize)
{
	// make sure init preserves priorities
	auto p = setup_picker("1111111", "       ", "1111111", "");

	TEST_EQUAL(p->want().num_pieces, 7);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	TEST_EQUAL(p->have().num_pieces, 0);

	p->set_piece_priority(piece_index_t(0), dont_download);
	TEST_EQUAL(p->want().num_pieces, 6);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	TEST_EQUAL(p->have().num_pieces, 0);

	p->we_have(piece_index_t(0));

	TEST_EQUAL(p->want().num_pieces, 6);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	TEST_EQUAL(p->have().num_pieces, 1);

	p->resize(blocks_per_piece, blocks_per_piece, blocks_per_piece * 7);
	TEST_EQUAL(p->piece_priority(piece_index_t(0)), dont_download);
	TEST_EQUAL(p->want().num_pieces, blocks_per_piece * 7 - 1);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	TEST_EQUAL(p->have().num_pieces, 0);
}

TORRENT_TEST(we_have_all)
{
	auto p = setup_picker("0123111", "  ** * ", "1234567", " 1234");

	p->we_have_all();

	TEST_EQUAL(p->want().num_pieces, 7);
	TEST_EQUAL(p->want().pad_blocks, 0);
	TEST_EQUAL(p->want().last_piece, true);

	TEST_EQUAL(p->have_want().num_pieces, 7);
	TEST_EQUAL(p->have_want().pad_blocks, 0);
	TEST_EQUAL(p->have_want().last_piece, true);

	TEST_EQUAL(p->have().num_pieces, 7);
	TEST_EQUAL(p->have().pad_blocks, 0);
	TEST_EQUAL(p->have().last_piece, true);
}

TORRENT_TEST(dont_pick_requested_blocks)
{
	// make sure requested blocks aren't picked
	auto p = setup_picker("1111111", "       ", "", "");
	auto picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	piece_block first = picked.front();
	p->mark_as_downloading(picked.front(), &peer_struct);
	TEST_CHECK(p->num_peers(picked.front()) == 1);
	picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() != first);
}

TORRENT_TEST(downloading_piece_priority)
{
	// make sure downloading pieces have higher priority
	auto p = setup_picker("1111111", "       ", "", "");
	auto picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	auto first = picked.front();
	p->mark_as_downloading(picked.front(), &peer_struct);
	TEST_CHECK(p->num_peers(picked.front()) == 1);
	picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() != first);
	TEST_CHECK(picked.front().piece_index == first.piece_index);
}

TORRENT_TEST(partial_piece_order_rarest_first)
{
	// when we're prioritizing partial pieces, make sure to first pick the
	// rarest of them. The blocks in this test are:
	// 0: [    ] avail: 1
	// 1: [x   ] avail: 1
	// 2: [xx  ] avail: 1
	// 3: [xxx ] avail: 2
	// 4: [    ] avail: 1
	// 5: [    ] avail: 1
	// 6: [xxxx] avail: 1
	// piece 6 does not have any blocks left to pick, even though piece 3 only
	// has a single block left before it completes, it is less rare than piece
	// 2. Piece 2 is the best pick in this case.
	auto p = setup_picker("1112111", "       ", "", "013700f");
	auto picked = pick_pieces(p, "*******", 1, 0, nullptr
		, options | piece_picker::prioritize_partials, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(piece_index_t(2), 2)
		|| picked.front() == piece_block(piece_index_t(2), 3));
}

TORRENT_TEST(partial_piece_order_most_complete)
{
	// as a tie breaker, make sure downloading pieces closer to completion have
	// higher priority. piece 3 is only 1 block from being completed, and should
	// be picked
	auto p = setup_picker("1111111", "       ", "", "013700f");
	auto picked = pick_pieces(p, "*******", 1, 0, nullptr
		, options | piece_picker::prioritize_partials, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(piece_index_t(3), 3));
}

TORRENT_TEST(partial_piece_order_sequential)
{
	// if we don't use rarest first when we prioritize partials, but instead use
	// sequential order, make sure we pick the right one
	auto p = setup_picker("1111111", "       ", "", "013700f");
	auto picked = pick_pieces(p, "*******", 1, 0, nullptr
		, piece_picker::sequential | piece_picker::prioritize_partials, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(piece_index_t(1), 1)
		|| picked.front() == piece_block(piece_index_t(1), 2)
		|| picked.front() == piece_block(piece_index_t(1), 3));
}

TORRENT_TEST(random_picking_downloading_piece)
{
	// make sure the random piece picker can still pick partial pieces
	auto p = setup_picker("1111111", "       ", "", "013700f");
	auto picked = pick_pieces(p, " ***  *", 1, 0, nullptr
		, {}, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(piece_index_t(1), 1)
		|| picked.front() == piece_block(piece_index_t(2), 2)
		|| picked.front() == piece_block(piece_index_t(3), 3));
}

TORRENT_TEST(random_picking_downloading_piece_prefer_contiguous)
{
	// make sure the random piece picker can still pick partial pieces
	// even when prefer_contiguous_blocks is set
	auto p = setup_picker("1111111", "       ", "", "013700f");
	auto picked = pick_pieces(p, " ***  *", 1, 4, nullptr
		, {}, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(piece_index_t(1), 1)
		|| picked.front() == piece_block(piece_index_t(2), 2)
		|| picked.front() == piece_block(piece_index_t(3), 3));
}

TORRENT_TEST(sequential_download)
{
	// test sequential download
	auto p = setup_picker("7654321", "       ", "", "");
	auto picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, nullptr
		, piece_picker::sequential, empty_vector);
	TEST_CHECK(int(picked.size()) == 7 * blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(i / blocks_per_piece)
			, i % blocks_per_piece));
}

TORRENT_TEST(reverse_sequential_download)
{
	// test reverse sequential download
	auto p = setup_picker("7654321", "       ", "", "");
	auto picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, nullptr
		, piece_picker::sequential | piece_picker::reverse, empty_vector);
	TEST_CHECK(int(picked.size()) == 7 * blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(6 - (i / blocks_per_piece))
			, i % blocks_per_piece));
}

TORRENT_TEST(priority_sequential_download)
{
	// test priority sequential download
	auto p = setup_picker("7654321", "       ", "1117071", "");
	auto picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, nullptr
		, piece_picker::sequential, empty_vector);

	// the piece with priority 0 was not picked, everything else should
	// be picked
	TEST_EQUAL(int(picked.size()), 6 * blocks_per_piece);

	// the first two pieces picked should be 3 and 5 since those have priority 7
	for (int i = 0; i < 2 * blocks_per_piece; ++i)
		TEST_CHECK(picked[std::size_t(i)].piece_index == piece_index_t(3) || picked[std::size_t(i)].piece_index == piece_index_t(5));

	int expected[] = {-1, -1, 0, 1, 2, 6};
	for (int i = 2 * blocks_per_piece; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[std::size_t(i)].piece_index, piece_index_t(expected[i / blocks_per_piece]));
}

TORRENT_TEST(cursors_sweep_up_we_have)
{
	// sweep up, we_have()
	auto p = setup_picker("7654321", "       ", "", "");
	for (piece_index_t i(0); i < piece_index_t(7); ++i)
	{
		TEST_EQUAL(p->cursor(), i);
		TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
		p->we_have(i);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
}

TORRENT_TEST(cursors_sweep_up_set_piece_priority)
{
	// sweep up, set_piece_priority()
	auto p = setup_picker("7654321", "       ", "", "");
	for (piece_index_t i(0); i < piece_index_t(7); ++i)
	{
		TEST_EQUAL(p->cursor(), i);
		TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
		p->set_piece_priority(i, dont_download);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
}

TORRENT_TEST(cursors_sweep_down_we_have)
{
	// sweep down, we_have()
	auto p = setup_picker("7654321", "       ", "", "");
	for (piece_index_t i(6); i >= piece_index_t(0); --i)
	{
		TEST_EQUAL(p->cursor(), piece_index_t(0));
		TEST_EQUAL(p->reverse_cursor(), next(i));
		p->we_have(i);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
}

TORRENT_TEST(cursors_sweep_down_set_piece_priority)
{
	// sweep down, set_piece_priority()
	auto p = setup_picker("7654321", "       ", "", "");
	for (piece_index_t i(6); i >= piece_index_t(0); --i)
	{
		TEST_EQUAL(p->cursor(), piece_index_t(0));
		TEST_EQUAL(p->reverse_cursor(), next(i));
		p->set_piece_priority(i, dont_download);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
}

TORRENT_TEST(cursors_sweep_in_set_priority)
{
	// sweep in, set_piece_priority()
	auto p = setup_picker("7654321", "       ", "", "");
	for (piece_index_t left(0), right(6); left <= piece_index_t(3)
		&& right >= piece_index_t(3); ++left, --right)
	{
		TEST_EQUAL(p->cursor(), left);
		TEST_EQUAL(p->reverse_cursor(), next(right));
		p->set_piece_priority(left, dont_download);
		p->set_piece_priority(right, dont_download);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
}

TORRENT_TEST(cursors_sweep_in_we_have)
{
	// sweep in, we_have()
	auto p = setup_picker("7654321", "       ", "", "");
	for (piece_index_t left(0), right(6); left <= piece_index_t(3)
		&& right >= piece_index_t(3); ++left, --right)
	{
		TEST_EQUAL(p->cursor(), left);
		TEST_EQUAL(p->reverse_cursor(), next(right));
		p->we_have(left);
		p->we_have(right);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
}

TORRENT_TEST(cursors_sweep_up_we_dont_have)
{
	auto p = setup_picker("7654321", "*******", "", "");
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
	for (piece_index_t i(0); i < piece_index_t(7); ++i)
	{
		p->we_dont_have(i);
		TEST_EQUAL(p->cursor(), piece_index_t(0));
		TEST_EQUAL(p->reverse_cursor(), next(i));
	}
	TEST_CHECK(!p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
}

TORRENT_TEST(cursors_sweep_down_we_dont_have)
{
	auto p = setup_picker("7654321", "*******", "", "");
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
	for (piece_index_t i(6); i >= piece_index_t(0); --i)
	{
		p->we_dont_have(i);
		TEST_EQUAL(p->cursor(), i);
		TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	}
	TEST_CHECK(!p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
}

TORRENT_TEST(cursors_sweep_out_we_dont_have)
{
	auto p = setup_picker("7654321", "*******", "", "");
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
	for (piece_index_t left(3), right(3);
		left >= piece_index_t(0) && right < piece_index_t(7);
		--left, ++right)
	{
		p->we_dont_have(left);
		p->we_dont_have(right);
		TEST_EQUAL(p->cursor(), left);
		TEST_EQUAL(p->reverse_cursor(), next(right));
	}
	TEST_CHECK(!p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
}

TORRENT_TEST(cursors)
{
	// test cursors
	auto p = setup_picker("7654321", "       ", "", "");
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->we_have(piece_index_t(1));
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->we_have(piece_index_t(0));
	TEST_EQUAL(p->cursor(), piece_index_t(2));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->we_have(piece_index_t(5));
	TEST_EQUAL(p->cursor(), piece_index_t(2));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->we_have(piece_index_t(6));
	TEST_EQUAL(p->cursor(), piece_index_t(2));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(5));
	p->we_have(piece_index_t(4));
	p->we_have(piece_index_t(3));
	p->we_have(piece_index_t(2));
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));

	p = setup_picker("7654321", "       ", "", "");
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->set_piece_priority(piece_index_t(1), dont_download);
	TEST_EQUAL(p->cursor(), piece_index_t(0));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->set_piece_priority(piece_index_t(0), dont_download);
	TEST_EQUAL(p->cursor(), piece_index_t(2));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->set_piece_priority(piece_index_t(5), dont_download);
	TEST_EQUAL(p->cursor(), piece_index_t(2));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(7));
	p->set_piece_priority(piece_index_t(6), dont_download);
	TEST_EQUAL(p->cursor(), piece_index_t(2));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(5));
	p->set_piece_priority(piece_index_t(4), dont_download);
	p->set_piece_priority(piece_index_t(3), dont_download);
	p->set_piece_priority(piece_index_t(2), dont_download);
	TEST_EQUAL(p->cursor(), piece_index_t(7));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(0));
	p->set_piece_priority(piece_index_t(3), low_priority);
	TEST_EQUAL(p->cursor(), piece_index_t(3));
	TEST_EQUAL(p->reverse_cursor(), piece_index_t(4));
}

TORRENT_TEST(piece_priorities)
{
	// test piece priorities
	auto p = setup_picker("5555555", "       ", "7654321", "");
	TEST_EQUAL(p->want().num_pieces, 7);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	p->set_piece_priority(piece_index_t(0), dont_download);
	TEST_EQUAL(p->want().num_pieces, 6);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	p->mark_as_finished({piece_index_t(0), 0}, nullptr);
	p->we_have(piece_index_t(0));
	TEST_EQUAL(p->want().num_pieces, 6);
	TEST_EQUAL(p->have_want().num_pieces, 0);
	TEST_EQUAL(p->have().num_pieces, 1);

	p->we_dont_have(piece_index_t(0));
	p->set_piece_priority(piece_index_t(0), top_priority);

	auto picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, nullptr
		, options, empty_vector);
	TEST_CHECK(int(picked.size()) == 7 * blocks_per_piece);

	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(i / blocks_per_piece), i % blocks_per_piece));

	// test changing priority on a piece we have
	p->we_have(piece_index_t(0));
	p->set_piece_priority(piece_index_t(0), dont_download);
	p->set_piece_priority(piece_index_t(0), low_priority);
	p->set_piece_priority(piece_index_t(0), dont_download);

	std::vector<download_priority_t> prios;
	p->piece_priorities(prios);
	TEST_CHECK(prios.size() == 7);
	download_priority_t prio_comp[] = {0_pri, 6_pri, 5_pri, 4_pri, 3_pri, 2_pri, 1_pri};
	TEST_CHECK(std::equal(prios.begin(), prios.end(), prio_comp));
}

TORRENT_TEST(restore_piece)
{
	// test restore_piece
	auto p = setup_picker("1234567", "       ", "", "");
	p->mark_as_finished({piece_index_t(0), 0}, nullptr);
	p->mark_as_finished({piece_index_t(0), 1}, nullptr);
	p->mark_as_finished({piece_index_t(0), 2}, nullptr);
	p->mark_as_finished({piece_index_t(0), 3}, nullptr);

	auto picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == piece_index_t(1));

	p->restore_piece(piece_index_t(0));
	picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == piece_index_t(0));

	p->mark_as_finished({piece_index_t(0), 0}, nullptr);
	p->mark_as_finished({piece_index_t(0), 1}, nullptr);
	p->mark_as_finished({piece_index_t(0), 2}, nullptr);
	p->mark_as_finished({piece_index_t(0), 3}, nullptr);
	p->set_piece_priority(piece_index_t(0), dont_download);

	picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == piece_index_t(1));

	p->restore_piece(piece_index_t(0));
	picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == piece_index_t(1));

	p->set_piece_priority(piece_index_t(0), top_priority);
	picked = pick_pieces(p, "*******", 1, 0, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == piece_index_t(0));
}

TORRENT_TEST(random_pick)
{
	// test random mode
	auto p = setup_picker("1234567", "       ", "1111122", "");
	std::set<piece_index_t> random_pieces;
	for (int i = 0; i < 100; ++i)
		random_pieces.insert(test_pick(p, {}));
	TEST_CHECK(random_pieces.size() == 7);

	random_pieces.clear();
	for (int i = 0; i < 7; ++i)
	{
		piece_index_t const piece = test_pick(p, {});
		p->we_have(piece);
		random_pieces.insert(piece);
	}
	TEST_CHECK(random_pieces.size() == 7);
}

TORRENT_TEST(picking_downloading_blocks)
{
	// make sure the piece picker will pick pieces that
	// are already requested from other peers if it has to
	auto p = setup_picker("1111111", "       ", "", "");
	p->mark_as_downloading({piece_index_t(2), 2}, &tmp1);
	p->mark_as_downloading({piece_index_t(1), 2}, &tmp1);

	std::vector<piece_block> picked;
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 7 * blocks_per_piece, 0, nullptr
		, piece_picker::prioritize_partials, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// don't pick both busy pieces, if there are already other blocks picked
	TEST_EQUAL(picked.size(), 7 * blocks_per_piece - 2);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 7 * blocks_per_piece, 0, nullptr
		, piece_picker::prioritize_partials
		| piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// don't pick both busy pieces, if there are already other blocks picked
	TEST_EQUAL(picked.size(), 7 * blocks_per_piece - 2);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 7 * blocks_per_piece, 0, nullptr
		, piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// don't pick both busy pieces, if there are already other blocks picked
	TEST_EQUAL(picked.size(), 7 * blocks_per_piece - 2);

	// make sure we still pick from a partial piece even when prefering whole pieces
	picked.clear();
	p->pick_pieces(string2vec(" *     "), picked, 1, blocks_per_piece, nullptr
		, piece_picker::rarest_first
			| piece_picker::align_expanded_pieces, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);
	TEST_CHECK(picked.size() >= 1 && picked[0].piece_index == piece_index_t(1));

	// don't pick locked pieces
	picked.clear();
	p->lock_piece(piece_index_t(1));
	p->pick_pieces(string2vec(" **    "), picked, 7, 0, nullptr
		, piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 3);
	TEST_CHECK(picked.size() >= 1 && picked[0].piece_index == piece_index_t(2));

	p->restore_piece(piece_index_t(1));
	p->mark_as_downloading({piece_index_t(2), 0}, &tmp1);
	p->mark_as_downloading({piece_index_t(2), 1}, &tmp1);
	p->mark_as_downloading({piece_index_t(2), 3}, &tmp1);
	p->mark_as_downloading({piece_index_t(1), 0}, &tmp1);
	p->mark_as_downloading({piece_index_t(1), 1}, &tmp1);
	p->mark_as_downloading({piece_index_t(1), 2}, &tmp1);
	p->mark_as_downloading({piece_index_t(1), 3}, &tmp1);

	picked.clear();
	p->pick_pieces(string2vec(" **    "), picked, 2, 0, nullptr
		, piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);

	picked.clear();
	p->pick_pieces(string2vec(" **    "), picked, 2 * blocks_per_piece, 0, nullptr
		, piece_picker::prioritize_partials, empty_vector, 0
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);

	picked.clear();
	p->pick_pieces(string2vec(" **    "), picked, 2 * blocks_per_piece, 0, nullptr
		, piece_picker::prioritize_partials, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);
}

TORRENT_TEST(clear_peer)
{
	// test clear_peer
	auto p = setup_picker("1123333", "       ", "", "");
	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1);
	p->mark_as_downloading({piece_index_t(0), 1}, &tmp2);
	p->mark_as_downloading({piece_index_t(0), 2}, &tmp3);
	p->mark_as_downloading({piece_index_t(1), 1}, &tmp1);
	p->mark_as_downloading({piece_index_t(2), 1}, &tmp2);
	p->mark_as_downloading({piece_index_t(3), 1}, &tmp3);

	std::vector<torrent_peer*> dls;
	torrent_peer* expected_dls1[] = {&tmp1, &tmp2, &tmp3, nullptr};
	torrent_peer* expected_dls2[] = {nullptr, &tmp1, nullptr, nullptr};
	torrent_peer* expected_dls3[] = {nullptr, &tmp2, nullptr, nullptr};
	torrent_peer* expected_dls4[] = {nullptr, &tmp3, nullptr, nullptr};
	torrent_peer* expected_dls5[] = {&tmp1, nullptr, &tmp3, nullptr};
	p->get_downloaders(dls, piece_index_t(0));
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls1));
	p->get_downloaders(dls, piece_index_t(1));
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls2));
	p->get_downloaders(dls, piece_index_t(2));
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls3));
	p->get_downloaders(dls, piece_index_t(3));
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls4));

	p->clear_peer(&tmp2);
	p->get_downloaders(dls, piece_index_t(0));
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls5));
}

TORRENT_TEST(have_all_have_none)
{
	// test have_all and have_none
	auto p = setup_picker("0123333", "*      ", "", "");
	auto dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(1, 5000 / 7));
	p->inc_refcount_all(&tmp8);
	dc = p->distributed_copies();
	TEST_CHECK(dc == std::make_pair(2, 5000 / 7));
	p->dec_refcount_all(&tmp8);
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(1, 5000 / 7));
	p->inc_refcount(piece_index_t(0), &tmp0);
	p->dec_refcount_all(&tmp0);
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(0, 6000 / 7));
	TEST_CHECK(test_pick(p) == piece_index_t(2));
}

TORRENT_TEST(have_all_have_none_seq_download)
{
	// test have_all and have_none
	auto p = setup_picker("0123333", "*      ", "", "");
	auto dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(1, 5000 / 7));
	p->inc_refcount_all(&tmp8);
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(2, 5000 / 7));
	TEST_CHECK(test_pick(p) == piece_index_t(1));
}

TORRENT_TEST(inc_ref_dec_ref)
{
	// test inc_ref and dec_ref
	auto p = setup_picker("1233333", "     * ", "", "");
	TEST_CHECK(test_pick(p) == piece_index_t(0));

	p->dec_refcount(piece_index_t(0), &tmp0);
	TEST_CHECK(test_pick(p) == piece_index_t(1));

	p->dec_refcount(piece_index_t(4), &tmp0);
	p->dec_refcount(piece_index_t(4), &tmp1);
	TEST_CHECK(test_pick(p) == piece_index_t(4));

	// decrease refcount on something that's not in the piece list
	p->dec_refcount(piece_index_t(5), &tmp0);
	p->inc_refcount(piece_index_t(5), &tmp0);

	typed_bitfield<piece_index_t> bits = string2vec("*      ");
	TEST_EQUAL(bits.get_bit(piece_index_t(0)), true);
	TEST_EQUAL(bits.get_bit(piece_index_t(1)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(2)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(3)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(4)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(5)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(6)), false);
	p->inc_refcount(bits, &tmp0);
	bits = string2vec("    *  ");

	TEST_EQUAL(bits.get_bit(piece_index_t(0)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(1)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(2)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(3)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(4)), true);
	TEST_EQUAL(bits.get_bit(piece_index_t(5)), false);
	TEST_EQUAL(bits.get_bit(piece_index_t(6)), false);
	p->dec_refcount(bits, &tmp2);
	TEST_EQUAL(test_pick(p), piece_index_t(0));
}

TORRENT_TEST(prefer_cnotiguous_blocks)
{
	// test prefer_contiguous_blocks
	auto p = setup_picker("1111111", "       ", "", "");
	auto picked = pick_pieces(p, "*******", 1, 3 * blocks_per_piece
		, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 3 * blocks_per_piece);
	piece_block b = picked.front();
	for (std::size_t i = 1; i < picked.size(); ++i)
	{
		TEST_CHECK(static_cast<int>(picked[i].piece_index) * blocks_per_piece + picked[i].block_index
			== static_cast<int>(b.piece_index) * blocks_per_piece + b.block_index + 1);
		b = picked[i];
	}

	picked = pick_pieces(p, "*******", 1, 3 * blocks_per_piece
		, nullptr, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 3 * blocks_per_piece);
	b = picked.front();
	for (std::size_t i = 1; i < picked.size(); ++i)
	{
		TEST_CHECK(static_cast<int>(picked[i].piece_index) * blocks_per_piece + picked[i].block_index
			== static_cast<int>(b.piece_index) * blocks_per_piece + b.block_index + 1);
		b = picked[i];
	}

	// make sure pieces that don't match the 'whole pieces' requirement
	// are picked if there's no other choice
	p = setup_picker("1111111", "       ", "", "");
	p->mark_as_downloading({piece_index_t(2), 2}, &tmp1);
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece - 1, blocks_per_piece
		, nullptr, options, empty_vector);
	TEST_CHECK(picked.size() == 7 * blocks_per_piece - 1);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(piece_index_t(2), 2)) == picked.end());
}

TORRENT_TEST(prefer_aligned_whole_pieces)
{
	auto p = setup_picker("2222221222222222", "                ", "", "");
	auto picked = pick_pieces(p, "****************", 1, 4 * blocks_per_piece, nullptr
		, options | piece_picker::align_expanded_pieces, empty_vector);

	// the piece picker should pick piece 5, and then align it to even 4 pieces
	// i.e. it should have picked pieces: 4,5,6,7
	print_pick(picked);
	TEST_EQUAL(picked.size(), 4 * blocks_per_piece);

	std::set<piece_index_t> picked_pieces;
	for (auto idx : picked) picked_pieces.insert(idx.piece_index);

	TEST_CHECK(picked_pieces.size() == 4);
	piece_index_t expected_pieces[] = {piece_index_t(4),piece_index_t(5),piece_index_t(6),piece_index_t(7)};
	TEST_CHECK(std::equal(picked_pieces.begin(), picked_pieces.end(), expected_pieces));
}

TORRENT_TEST(parole_mode)
{
	// test parole mode
	auto p = setup_picker("3333133", "       ", "", "");
	p->mark_as_finished({piece_index_t(0), 0}, nullptr);
	auto picked = pick_pieces(p, "*******", 1, blocks_per_piece, nullptr

		, options | piece_picker::on_parole | piece_picker::prioritize_partials, empty_vector);
	TEST_EQUAL(int(picked.size()), blocks_per_piece - 1);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(0), i + 1));

	// make sure that the partial piece is not picked by a
	// peer that is has not downloaded/requested the other blocks
	picked = pick_pieces(p, "*******", 1, blocks_per_piece
		, &peer_struct
		, options | piece_picker::on_parole | piece_picker::prioritize_partials, empty_vector);
	TEST_EQUAL(int(picked.size()), blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(4), i));
}

TORRENT_TEST(suggested_pieces)
{
	// test suggested pieces
	auto p = setup_picker("1111222233334444", "                ", "", "");
	int v[] = {1, 5};
	const std::vector<piece_index_t> suggested_pieces(v, v + 2);

	auto picked = pick_pieces(p, "****************", 1, blocks_per_piece
		, nullptr, options, suggested_pieces);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(1), i));
	p->set_piece_priority(piece_index_t(0), dont_download);
	p->set_piece_priority(piece_index_t(1), dont_download);
	p->set_piece_priority(piece_index_t(2), dont_download);
	p->set_piece_priority(piece_index_t(3), dont_download);

	picked = pick_pieces(p, "****************", 1, blocks_per_piece
		, nullptr, options, suggested_pieces);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(5), i));

	p = setup_picker("1111222233334444", "****            ", "", "");
	picked = pick_pieces(p, "****************", 1, blocks_per_piece
		, nullptr, options, suggested_pieces);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[std::size_t(i)] == piece_block(piece_index_t(5), i));
}

TORRENT_TEST(bitfield_optimization)
{
	// test bitfield optimization
	// we have less than half of the pieces
	auto p = setup_picker("2122222211221222", "                ", "", "");
	// make sure it's not dirty
	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);
	print_availability(p);
	p->dec_refcount(string2vec("**  **  **  *   "), &tmp0);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1022112200220222"));
	// make sure it's not dirty
	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);
	p->inc_refcount(string2vec(" **  **  *   *  "), &tmp8);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1132123201220322"));
}

TORRENT_TEST(seed_optimization)
{
	// test seed optimizaton
	auto p = setup_picker("0000000000000000", "                ", "", "");

	// make sure it's not dirty
	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);

	p->inc_refcount_all(&tmp0);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1111111111111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);
	p->dec_refcount(string2vec("  ****  **      "), &tmp0);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1100001100111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);
	p->inc_refcount(string2vec("  ****  **      "), &tmp0);
	TEST_CHECK(verify_availability(p, "1111111111111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);
	p->dec_refcount_all(&tmp0);
	TEST_CHECK(verify_availability(p, "0000000000000000"));

	p->inc_refcount_all(&tmp1);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1111111111111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, nullptr);
	p->dec_refcount(piece_index_t(3), &tmp1);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1110111111111111"));

	p->inc_refcount(string2vec("****************"), &tmp2);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "2221222222222222"));

	p->inc_refcount(string2vec("* * * * * * * * "), &tmp3);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "3231323232323232"));

	p->dec_refcount(string2vec("****************"), &tmp2);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "2120212121212121"));

	p->dec_refcount(string2vec("* * * * * * * * "), &tmp3);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1110111111111111"));
}

TORRENT_TEST(reversed_peers)
{
	// test reversed peers
	auto p = setup_picker("3333333", "  *****", "", "");

	// a reversed peer picked a block from piece 0
	// This should make the piece reversed
	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1
		, piece_picker::reverse);

	TEST_EQUAL(test_pick(p, piece_picker::rarest_first), piece_index_t(1));

	// make sure another reversed peer pick the same piece
	TEST_EQUAL(test_pick(p, piece_picker::rarest_first | piece_picker::reverse), piece_index_t(0));
}

TORRENT_TEST(reversed_piece_upgrade)
{
	// test reversed pieces upgrading to normal pieces
	auto p = setup_picker("3333333", "  *****", "", "");

	// make piece 0 partial and reversed
	p->mark_as_downloading({piece_index_t(0), 1}, &tmp1
		, piece_picker::reverse);
	TEST_EQUAL(test_pick(p), piece_index_t(1));

	// now have a regular peer pick the reversed block. It should now
	// have turned into a regular one and be prioritized
	p->mark_as_downloading({piece_index_t(0), 2}, &tmp1);
	TEST_EQUAL(test_pick(p), piece_index_t(0));
}

TORRENT_TEST(reversed_piece_downgrade)
{
// test pieces downgrading to reversed pieces
// now make sure a piece can be demoted to reversed if there are no
// other outstanding requests

	auto p = setup_picker("3333333", "       ", "", "");

	// make piece 0 partial and not reversed
	p->mark_as_finished({piece_index_t(0), 1}, &tmp1);

	// a reversed peer picked a block from piece 0
	// This should make the piece reversed
	p->mark_as_downloading({piece_index_t(0), 0}, &tmp1
		, piece_picker::reverse);

	TEST_EQUAL(test_pick(p, piece_picker::rarest_first | piece_picker::reverse), piece_index_t(0));
}

TORRENT_TEST(piece_stats)
{
	auto p = setup_picker("3456789", "*      ", "", "0300000");

	piece_picker::piece_stats_t stat = p->piece_stats(piece_index_t(0));
	TEST_EQUAL(stat.peer_count, 3);
	TEST_EQUAL(stat.have, 1);
	TEST_EQUAL(stat.downloading, 0);

	stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.peer_count, 4);
	TEST_EQUAL(stat.have, 0);
	TEST_EQUAL(stat.downloading, 1);
}

TORRENT_TEST(piece_passed)
{
	auto p = setup_picker("1111111", "*      ", "", "0300000");

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->have().num_pieces, 1);

	p->piece_passed(piece_index_t(1));
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->have().num_pieces, 1);

	p->we_have(piece_index_t(1));
	TEST_EQUAL(p->have().num_pieces, 2);

	p->mark_as_finished({piece_index_t(2), 0}, &tmp1);
	p->piece_passed(piece_index_t(2));
	TEST_EQUAL(p->num_passed(), 3);
	// just because the hash check passed doesn't mean
	// we "have" the piece. We need to write it to disk first
	TEST_EQUAL(p->have().num_pieces, 2);

	// piece 2 already passed the hash check, as soon as we've
	// written all the blocks to disk, we should have that piece too
	p->mark_as_finished({piece_index_t(2), 1}, &tmp1);
	p->mark_as_finished({piece_index_t(2), 2}, &tmp1);
	p->mark_as_finished({piece_index_t(2), 3}, &tmp1);
	TEST_EQUAL(p->have().num_pieces, 3);
	TEST_EQUAL(p->have_piece(piece_index_t(2)), true);
}

TORRENT_TEST(piece_passed_causing_we_have)
{
	auto p = setup_picker("1111111", "*      ", "", "0700000");

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->have().num_pieces, 1);

	p->mark_as_finished({piece_index_t(1), 3}, &tmp1);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->have().num_pieces, 1);

	p->piece_passed(piece_index_t(1));
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->have().num_pieces, 2);
}

TORRENT_TEST(break_one_seed)
{
	auto p = setup_picker("0000000", "*      ", "", "0700000");
	p->inc_refcount_all(&tmp1);
	p->inc_refcount_all(&tmp2);
	p->inc_refcount_all(&tmp3);

	TEST_EQUAL(p->piece_stats(piece_index_t(0)).peer_count, 3);

	p->dec_refcount(piece_index_t(0), &tmp1);

	TEST_EQUAL(p->piece_stats(piece_index_t(0)).peer_count, 2);
	TEST_EQUAL(p->piece_stats(piece_index_t(1)).peer_count, 3);
	TEST_EQUAL(p->piece_stats(piece_index_t(2)).peer_count, 3);
	TEST_EQUAL(p->piece_stats(piece_index_t(3)).peer_count, 3);
}

TORRENT_TEST(we_dont_have2)
{
	auto p = setup_picker("1111111", "* *    ", "1101111", "");
	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(2)), true);
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->have().num_pieces, 2);
	TEST_EQUAL(p->have_want().num_pieces, 1);
	TEST_EQUAL(p->want().num_pieces, 6);

	p->we_dont_have(piece_index_t(0));

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), false);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(2)), true);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->have().num_pieces, 1);
	TEST_EQUAL(p->have_want().num_pieces, 0);

	p = setup_picker("1111111", "* *    ", "1101111", "");
	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(2)), true);
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->have().num_pieces, 2);
	TEST_EQUAL(p->have_want().num_pieces, 1);
	TEST_EQUAL(p->want().num_pieces, 6);

	p->we_dont_have(piece_index_t(2));

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(2)), false);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->have().num_pieces, 1);
	TEST_EQUAL(p->have_want().num_pieces, 1);
	TEST_EQUAL(p->want().num_pieces, 6);
}

TORRENT_TEST(dont_have_but_passed_hash_check)
{
	auto p = setup_picker("1111111", "* *    ", "1101111", "0200000");

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->have_piece(piece_index_t(0)), true);
	TEST_EQUAL(p->have_piece(piece_index_t(1)), false);

	p->piece_passed(piece_index_t(1));

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), true);
	TEST_EQUAL(p->have_piece(piece_index_t(1)), false);

	p->we_dont_have(piece_index_t(1));

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->have_piece(piece_index_t(1)), false);
}

TORRENT_TEST(write_failed)
{
	auto p = setup_picker("1111111", "* *    ", "1101111", "0200000");

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->have_piece(piece_index_t(1)), false);

	p->piece_passed(piece_index_t(1));

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), true);
	TEST_EQUAL(p->have_piece(piece_index_t(1)), false);

	p->mark_as_writing({piece_index_t(1), 0}, &tmp1);
	p->write_failed({piece_index_t(1), 0});

	TEST_EQUAL(p->has_piece_passed(piece_index_t(0)), true);
	TEST_EQUAL(p->has_piece_passed(piece_index_t(1)), false);
	TEST_EQUAL(p->have_piece(piece_index_t(1)), false);

	// make sure write_failed() and lock_piece() actually
	// locks the piece, and that it won't be picked.
	// also make sure restore_piece() unlocks it and makes
	// it available for picking again.

	auto picked = pick_pieces(p, " *     ", 1, blocks_per_piece, nullptr);
	TEST_EQUAL(picked.size(), 0);

	p->restore_piece(piece_index_t(1));

	picked = pick_pieces(p, " *     ", 1, blocks_per_piece, nullptr);
	TEST_EQUAL(picked.size(), blocks_per_piece);

	// locking pieces only works on partial pieces
	p->mark_as_writing({piece_index_t(1), 0}, &tmp1);
	p->lock_piece(piece_index_t(1));

	picked = pick_pieces(p, " *     ", 1, blocks_per_piece, nullptr);
	TEST_EQUAL(picked.size(), 0);
}

TORRENT_TEST(write_failed_clear_piece)
{
	auto p = setup_picker("1111111", "* *    ", "1101111", "");

	auto stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.downloading, 0);

	p->mark_as_writing({piece_index_t(1), 0}, &tmp1);

	stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.downloading, 1);

	p->write_failed({piece_index_t(1), 0});

	stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.downloading, 0);
}

TORRENT_TEST(mark_as_canceled)
{
	auto p = setup_picker("1111111", "* *    ", "1101111", "");

	auto stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.downloading, 0);

	p->mark_as_writing({piece_index_t(1), 0}, &tmp1);

	stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.downloading, 1);

	p->mark_as_canceled({piece_index_t(1), 0}, &tmp1);
	stat = p->piece_stats(piece_index_t(1));
	TEST_EQUAL(stat.downloading, 0);
}

TORRENT_TEST(get_download_queue)
{
	auto picker = setup_picker("1111111", "       ", "1101111", "0327000");

	auto const downloads = picker->get_download_queue();

	// the download queue should have piece 1, 2 and 3 in it
	TEST_EQUAL(downloads.size(), 3);

	TEST_EQUAL(std::count_if(downloads.begin(), downloads.end()
		, [](piece_picker::downloading_piece const& p) { return p.index == piece_index_t(1); }), 1);
	TEST_EQUAL(std::count_if(downloads.begin(), downloads.end()
		, [](piece_picker::downloading_piece const& p) { return p.index == piece_index_t(2); }), 1);
	TEST_EQUAL(std::count_if(downloads.begin(), downloads.end()
		, [](piece_picker::downloading_piece const& p) { return p.index == piece_index_t(3); }), 1);
}

TORRENT_TEST(get_download_queue_size)
{
	auto p = setup_picker("1111111", "       ", "1111111", "0327ff0");

	TEST_EQUAL(p->get_download_queue_size(), 5);

	p->set_piece_priority(piece_index_t(1), dont_download);

	int partial;
	int full;
	int finished;
	int zero_prio;
	p->get_download_queue_sizes(&partial, &full, &finished, &zero_prio);

	TEST_EQUAL(partial, 2);
	TEST_EQUAL(full, 0);
	TEST_EQUAL(finished, 2);
	TEST_EQUAL(zero_prio, 1);
}

TORRENT_TEST(time_critical_mode)
{
	auto p = setup_picker("1111111", "       ", "1654741", "0352000");

	// rarest-first
	auto picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::rarest_first | piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));

	// reverse rarest-first
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::reverse | piece_picker::rarest_first
		| piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));

	// sequential
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::sequential | piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));

	// reverse sequential
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::reverse | piece_picker::sequential
		| piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));

	// just critical
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));

	// prioritize_partials
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::prioritize_partials | piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));

	// even when a non-critical piece is suggested should we ignore it
	int v[] = {1, 5};
	const std::vector<piece_index_t> suggested_pieces(v, v + 2);

	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::rarest_first | piece_picker::time_critical_mode
		, suggested_pieces);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[0].piece_index, piece_index_t(4));
}

TORRENT_TEST(reprioritize_downloading)
{
	auto p = setup_picker("1111111", "       ", "", "");
	bool ret;

	ret = p->mark_as_downloading({piece_index_t(0), 0}, tmp_peer);
	TEST_EQUAL(ret, true);
	p->mark_as_finished({piece_index_t(0), 1}, tmp_peer);
	ret = p->mark_as_writing({piece_index_t(0), 2}, tmp_peer);
	TEST_EQUAL(ret, true);

	// make sure we pick the partial piece (i.e. piece 0)
	TEST_EQUAL(test_pick(p, piece_picker::rarest_first | piece_picker::prioritize_partials), piece_index_t(0));

	// set the priority of the piece to 0 (while downloading it)
	ret = p->set_piece_priority(piece_index_t(0), dont_download);
	TEST_EQUAL(ret, true);

	// make sure we _DON'T_ pick the partial piece, since it has priority zero
	piece_index_t const picked_piece = test_pick(p, piece_picker::rarest_first
		| piece_picker::prioritize_partials);
	TEST_NE(picked_piece, piece_index_t(-1));
	TEST_NE(picked_piece, piece_index_t(0));

	// set the priority of the piece back to 1. It should now be the best pick
	// again (since it's partial)
	ret = p->set_piece_priority(piece_index_t(0), low_priority);
	TEST_EQUAL(ret, true);

	// make sure we pick the partial piece
	TEST_EQUAL(test_pick(p, piece_picker::rarest_first
		| piece_picker::prioritize_partials), piece_index_t(0));
}

TORRENT_TEST(reprioritize_fully_downloading)
{
	auto p = setup_picker("1111111", "       ", "", "");
	bool ret;

	for (int i = 0; i < blocks_per_piece; ++i)
	{
		ret = p->mark_as_downloading(piece_block(piece_index_t(0), i), tmp_peer);
		TEST_EQUAL(ret, true);
	}

	// make sure we _DON'T_ pick the downloading piece
	{
		piece_index_t const picked_piece = test_pick(p, piece_picker::rarest_first
			| piece_picker::prioritize_partials);
		TEST_NE(picked_piece, piece_index_t(-1));
		TEST_NE(picked_piece, piece_index_t(0));
	}

	// set the priority of the piece to 0 (while downloading it)
	ret = p->set_piece_priority(piece_index_t(0), dont_download);
	TEST_EQUAL(ret, true);

	// make sure we still _DON'T_ pick the downloading piece
	{
		piece_index_t const picked_piece = test_pick(p, piece_picker::rarest_first
			| piece_picker::prioritize_partials);
		TEST_NE(picked_piece, piece_index_t(-1));
		TEST_NE(picked_piece, piece_index_t(0));
	}

	// set the priority of the piece back to 1. It should now be the best pick
	// again (since it's partial)
	ret = p->set_piece_priority(piece_index_t(0), low_priority);
	TEST_EQUAL(ret, true);

	// make sure we still _DON'T_ pick the downloading piece
	{
		piece_index_t const picked_piece = test_pick(p, piece_picker::rarest_first
			| piece_picker::prioritize_partials);
		TEST_NE(picked_piece, piece_index_t(-1));
		TEST_NE(picked_piece, piece_index_t(0));
	}
}

TORRENT_TEST(download_filtered_piece)
{
	auto p = setup_picker("1111111", "       ", "", "");
	bool ret;

	// set the priority of the piece to 0
	ret = p->set_piece_priority(piece_index_t(0), dont_download);
	TEST_EQUAL(ret, true);

	// make sure we _DON'T_ pick piece 0
	{
		piece_index_t const picked_piece = test_pick(p, piece_picker::rarest_first
			| piece_picker::prioritize_partials);
		TEST_NE(picked_piece, piece_index_t(-1));
		TEST_NE(picked_piece, piece_index_t(0));
	}

	// then mark it for downloading
	ret = p->mark_as_downloading({piece_index_t(0), 0}, tmp_peer);
	TEST_EQUAL(ret, true);
	p->mark_as_finished({piece_index_t(0), 1}, tmp_peer);
	ret = p->mark_as_writing({piece_index_t(0), 2}, tmp_peer);
	TEST_EQUAL(ret, true);

	{
		// we still should not pick it
		piece_index_t const picked_piece = test_pick(p, piece_picker::rarest_first
			| piece_picker::prioritize_partials);
		TEST_NE(picked_piece, piece_index_t(-1));
		TEST_NE(picked_piece, piece_index_t(0));
	}

	// set the priority of the piece back to 1. It should now be the best pick
	// again (since it's partial)
	ret = p->set_piece_priority(piece_index_t(0), low_priority);
	TEST_EQUAL(ret, true);

	// make sure we pick piece 0
	TEST_EQUAL(test_pick(p, piece_picker::rarest_first | piece_picker::prioritize_partials), piece_index_t(0));
}

TORRENT_TEST(mark_as_pad)
{
	auto p = setup_picker("1111111", "       ", "4444444", "");
	piece_block const bl(piece_index_t{2}, 0);
	p->mark_as_pad(bl);

	bool ret = p->mark_as_downloading({piece_index_t{2}, 1}, tmp_peer);
	TEST_EQUAL(ret, true);

	auto dl = p->get_download_queue();

	TEST_EQUAL(dl.size(), 1);
	TEST_EQUAL(dl[0].finished, 1);
	TEST_EQUAL(dl[0].writing, 0);
	TEST_EQUAL(dl[0].requested, 1);
	TEST_EQUAL(dl[0].index, piece_index_t{2});

	auto blocks = p->blocks_for_piece(dl[0]);
	TEST_EQUAL(blocks[0].state, piece_picker::block_info::state_finished);
	TEST_EQUAL(blocks[1].state, piece_picker::block_info::state_requested);
	TEST_EQUAL(blocks[2].state, piece_picker::block_info::state_none);
	TEST_EQUAL(blocks[3].state, piece_picker::block_info::state_none);
}

TORRENT_TEST(mark_as_pad_downloading)
{
	auto p = setup_picker("1111111", "       ", "4444444", "");
	piece_block const bl(piece_index_t{2}, 0);
	p->mark_as_pad(bl);

	bool ret = p->mark_as_downloading({piece_index_t{2}, 0}, tmp_peer);
	TEST_EQUAL(ret, false);

	auto dl = p->get_download_queue();

	TEST_EQUAL(dl.size(), 1);
	TEST_EQUAL(dl[0].finished, 1);
	TEST_EQUAL(dl[0].writing, 0);
	TEST_EQUAL(dl[0].requested, 0);
	TEST_EQUAL(dl[0].index, piece_index_t{2});

	auto blocks = p->blocks_for_piece(dl[0]);
	TEST_EQUAL(blocks[0].state, piece_picker::block_info::state_finished);
	TEST_EQUAL(blocks[1].state, piece_picker::block_info::state_none);
	TEST_EQUAL(blocks[2].state, piece_picker::block_info::state_none);
	TEST_EQUAL(blocks[3].state, piece_picker::block_info::state_none);
}

TORRENT_TEST(mark_as_pad_seeding)
{
	auto p = setup_picker("1", " ", "4", "");
	p->mark_as_pad({piece_index_t{0}, 0});
	p->mark_as_pad({piece_index_t{0}, 1});
	p->mark_as_pad({piece_index_t{0}, 2});

	TEST_CHECK(!p->is_seeding());

	p->mark_as_finished({piece_index_t{0}, 3}, tmp_peer);

	TEST_CHECK(!p->is_seeding());
	p->piece_passed(piece_index_t{0});
	TEST_CHECK(p->is_seeding());
}

TORRENT_TEST(mark_as_pad_whole_piece_seeding)
{
	auto p = setup_picker("11", "  ", "44", "");
	p->mark_as_pad({piece_index_t{0}, 0});
	p->mark_as_pad({piece_index_t{0}, 1});
	p->mark_as_pad({piece_index_t{0}, 2});
	p->mark_as_pad({piece_index_t{0}, 3});
	TEST_CHECK(p->have_piece(piece_index_t{0}));

	TEST_CHECK(!p->is_seeding());

	p->mark_as_finished({piece_index_t{1}, 0}, nullptr);
	p->mark_as_finished({piece_index_t{1}, 1}, nullptr);
	p->mark_as_finished({piece_index_t{1}, 2}, nullptr);
	p->mark_as_finished({piece_index_t{1}, 3}, nullptr);

	TEST_CHECK(!p->is_seeding());
	p->piece_passed(piece_index_t{1});
	TEST_CHECK(p->is_seeding());
}

TORRENT_TEST(pad_blocks_in_piece)
{
	auto p = setup_picker("11", "  ", "44", "");
	p->mark_as_pad({piece_index_t{0}, 0});
	p->mark_as_pad({piece_index_t{0}, 1});
	p->mark_as_pad({piece_index_t{0}, 2});

	TEST_EQUAL(p->pad_blocks_in_piece(piece_index_t{0}), 3);
	TEST_EQUAL(p->pad_blocks_in_piece(piece_index_t{1}), 0);
}

TORRENT_TEST(pad_blocks_in_last_piece)
{
	auto p = setup_picker("11", "  ", "44", "");
	p->mark_as_pad({piece_index_t{1}, 0});
	p->mark_as_pad({piece_index_t{1}, 1});
	p->mark_as_pad({piece_index_t{1}, 2});

	TEST_EQUAL(p->pad_blocks_in_piece(piece_index_t{1}), 3);
	TEST_EQUAL(p->pad_blocks_in_piece(piece_index_t{0}), 0);
}

namespace {
void validate_piece_count(piece_count const& c)
{
	// it's an impossible combination to have 0 pieces, but still have one of them be the last piece
	TEST_CHECK(!(c.num_pieces == 0 && c.last_piece == true));

	// if we have 0 pieces, we can't have any pad blocks either
	TEST_CHECK(!(c.num_pieces == 0 && c.pad_blocks > 0));

	// if we have all pieces, we must also have the last one
	TEST_CHECK(!(c.num_pieces == 4 && c.last_piece == false));
}

void validate_all_pieces(piece_count const& c)
{
	TEST_EQUAL(c.last_piece, true);
	TEST_EQUAL(c.num_pieces, 4);
	TEST_EQUAL(c.pad_blocks, 3);
}

void validate_no_pieces(piece_count const& c)
{
	TEST_EQUAL(c.last_piece, false);
	TEST_EQUAL(c.num_pieces, 0);
	TEST_EQUAL(c.pad_blocks, 0);
}
}

TORRENT_TEST(pad_blocks_all_filtered)
{
	auto p = setup_picker("1111", "    ", "0000", "");
	p->mark_as_pad({piece_index_t{1}, 0});
	p->mark_as_pad({piece_index_t{1}, 1});
	p->mark_as_pad({piece_index_t{2}, 0});

	validate_piece_count(p->all_pieces());
	validate_piece_count(p->have());
	validate_piece_count(p->have_want());
	validate_piece_count(p->want());

	validate_all_pieces(p->all_pieces());
	validate_no_pieces(p->have());
	validate_no_pieces(p->have_want());
	validate_no_pieces(p->want());
}

TORRENT_TEST(pad_blocks_all_wanted)
{
	auto p = setup_picker("1111", "    ", "4444", "");
	p->mark_as_pad({piece_index_t{1}, 0});
	p->mark_as_pad({piece_index_t{1}, 1});
	p->mark_as_pad({piece_index_t{2}, 0});

	validate_piece_count(p->all_pieces());
	validate_piece_count(p->have());
	validate_piece_count(p->have_want());
	validate_piece_count(p->want());

	validate_all_pieces(p->all_pieces());
	validate_all_pieces(p->want());
	validate_no_pieces(p->have());
	validate_no_pieces(p->have_want());
}

TORRENT_TEST(pad_blocks_some_wanted)
{
	auto p = setup_picker("1111", "    ", "0404", "");
	p->mark_as_pad({piece_index_t{1}, 0});
	p->mark_as_pad({piece_index_t{1}, 1});
	p->mark_as_pad({piece_index_t{2}, 0});

	validate_piece_count(p->all_pieces());
	validate_piece_count(p->have());
	validate_piece_count(p->have_want());
	validate_piece_count(p->want());

	validate_all_pieces(p->all_pieces());
	validate_no_pieces(p->have());
	validate_no_pieces(p->have_want());

	TEST_EQUAL(p->want().num_pieces, 2);
	TEST_EQUAL(p->want().last_piece, true);
	TEST_EQUAL(p->want().pad_blocks, 2);
}

namespace {

std::vector<piece_block> full_piece(int const p, int const blocks)
{
	std::vector<piece_block> ret;
	for (int i = 0;i < blocks; ++i)
		ret.push_back(piece_block(piece_index_t{p}, i));
	return ret;
}
void mark_downloading(std::shared_ptr<piece_picker> const& p, std::vector<piece_block> const blocks
	, torrent_peer* const peer, picker_options_t const opts)
{
	for (auto const& b : blocks)
		p->mark_as_downloading(b, peer, opts);
}
}

TORRENT_TEST(piece_extent_affinity)
{
	int const blocks = 64;
	// these are 2 extents. the first 4 pieces and the last 4 pieces
	auto const have_none = "        ";
	auto const have_all  = "********";

	auto p = setup_picker("33133233", have_none, "", "", blocks);

	std::vector<piece_block> picked = pick_pieces(p, have_all, blocks, 0, &tmp0
		, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(2, blocks));
	mark_downloading(p, full_piece(2, blocks), &tmp0, options | piece_picker::piece_extent_affinity);

	// without the piece_extent_affinity, we would pick piece 5, because of
	// availability
	picked = pick_pieces(p, have_all, blocks, 0, &tmp1);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(5, blocks));
	mark_downloading(p, full_piece(5, blocks), &tmp0, options | piece_picker::piece_extent_affinity);

	// with piece_extent_affinity, we would pick piece 0, because it's the same
	// extent as the piece we just picked
	picked = pick_pieces(p, have_all, blocks, 0, &tmp2, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(0, blocks));
	mark_downloading(p, full_piece(0, blocks), &tmp0, options | piece_picker::piece_extent_affinity);

	// then we should pick piece 1
	picked = pick_pieces(p, have_all, blocks, 0, &tmp3, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(1, blocks));
	mark_downloading(p, full_piece(1, blocks), &tmp0, options | piece_picker::piece_extent_affinity);

	// then we should pick piece 3. The last piece of the extent
	picked = pick_pieces(p, have_all, blocks, 0, &tmp4, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(3, blocks));
	mark_downloading(p, full_piece(3, blocks), &tmp0, options | piece_picker::piece_extent_affinity);
}

TORRENT_TEST(piece_extent_affinity_priority)
{
	int const blocks = 64;
	auto const have_none = "        ";
	auto const have_all  = "********";

	auto p = setup_picker("33333233", have_none, "43444444", "", blocks);
	// we pick piece 2. Since piece 1 has a different priority this should not
	// create an affinity for the extent
	mark_downloading(p, full_piece(2, blocks), &tmp0, options | piece_picker::piece_extent_affinity);

	// so next piece to be picked will *not* be the extent, but piece 5, which
	// has the lowest availability

	std::vector<piece_block> picked = pick_pieces(p, have_all, blocks, 0, &tmp1
		, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(5, blocks));
}

TORRENT_TEST(piece_extent_affinity_large_pieces)
{
	int const blocks = 256;
	auto const have_none = "        ";
	auto const have_all  = "********";

	auto p = setup_picker("33333233", have_none, "", "", blocks);
	// we pick piece 2. Since the pieces are so large (4 MiB), there is no
	// affinity for piece extents.
	mark_downloading(p, full_piece(2, blocks), &tmp0, options | piece_picker::piece_extent_affinity);

	// so next piece to be picked will *not* be the extent, but piece 5, which
	// has the next lowest availability
	std::vector<piece_block> picked = pick_pieces(p, have_all, blocks, 0, &tmp1
		, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(5, blocks));
}

TORRENT_TEST(piece_extent_affinity_active_limit)
{
	// an extent is two pieces wide, 6 extents total.
	// make ure we limit the number of extents to 5
	int const blocks = 128;
	auto const have_none = "            ";

	auto p = setup_picker("333333333333", have_none, "444444444455", "", blocks);
	// open up the first 5 extents
	mark_downloading(p, full_piece(0, blocks), &tmp0, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(2, blocks), &tmp1, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(4, blocks), &tmp2, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(6, blocks), &tmp3, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(8, blocks), &tmp4, options | piece_picker::piece_extent_affinity);

	// this should not open up another extent. We should still have a bias
	// towards pieces 1, 3, 5, 7 and 9.
	mark_downloading(p, full_piece(10, blocks), &tmp5, options | piece_picker::piece_extent_affinity);

	// a peer that only has piece 0, 1, 10, 11, will always pick 1, never 11,
	// even though 10 and 11 have higher priority

	std::vector<piece_block> picked = pick_pieces(p, "**        **", blocks, 0, &tmp1
		, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(1, blocks));
}

TORRENT_TEST(piece_extent_affinity_clear_done)
{
	// an extent is two pieces wide, 7 extents total.
	// make sure we remove an active extent when we have all the pieces, and
	// allow a new extent to be added
	int const blocks = 128;
	auto const have_none = "              ";

	auto p = setup_picker("33333333333333", have_none, "44444444444455", "", blocks);
	// open up the first 5 extents
	mark_downloading(p, full_piece(0, blocks), &tmp0, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(2, blocks), &tmp1, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(4, blocks), &tmp2, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(6, blocks), &tmp3, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(8, blocks), &tmp4, options | piece_picker::piece_extent_affinity);

	// now all 5 extents are in use, if we finish a whole extent, it should be
	// removed from the list
	p->we_have(piece_index_t{0});
	p->we_have(piece_index_t{1});

	// we need to invoke the piece picker once to detect and reap this full
	// extent
	pick_pieces(p, "**************", blocks, 0, &tmp1, options | piece_picker::piece_extent_affinity);

	// this *should* open up another extent. We should still have a bias
	// towards pieces 1, 3, 5, 7 and 9.
	mark_downloading(p, full_piece(10, blocks), &tmp5, options | piece_picker::piece_extent_affinity);

	// a peer that only has piece 10, 11, 12, 13 will always pick 11, since it's
	// part of an extent that was just opened, never 12 or 13 even though they
	// have higher priority
	std::vector<piece_block> picked = pick_pieces(p, "          ****", blocks, 0, &tmp1
		, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(11, blocks));
}

TORRENT_TEST(piece_extent_affinity_no_duplicates)
{
	// an extent is 8 pieces wide, 3 extents total.
	// make sure that downloading pieces from the same extent don't create
	// multiple entries in the recent-extent list, but they all use a single
	// entry
	int const blocks = 32;
	auto const have_none = "                        ";

	auto p = setup_picker("333333333333333333333333", have_none
		, "444444444444444444444455", "", blocks);
	// download 5 pieces from the first extent
	mark_downloading(p, full_piece(0, blocks), &tmp0, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(2, blocks), &tmp1, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(4, blocks), &tmp2, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(6, blocks), &tmp3, options | piece_picker::piece_extent_affinity);
	mark_downloading(p, full_piece(1, blocks), &tmp4, options | piece_picker::piece_extent_affinity);

	// since all these belong to the same extent (0), there should be a single
	// entry in the recent extent list. Make sure that it's possible to open up a
	// second extent, to show that all 5 entries weren't used up by 5 duplicates
	// of 0.
	// opens up extent 1
	mark_downloading(p, full_piece(8, blocks), &tmp5, options | piece_picker::piece_extent_affinity);

	// now, from a peer that doesn't have anything from the first extent, still
	// pick from the second extent even though the last two pieces have higher
	// priority.
	std::vector<piece_block> picked = pick_pieces(p, "        ****************", blocks, 0, &tmp1
		, options | piece_picker::piece_extent_affinity);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(picked == full_piece(9, blocks));
}

//TODO: 2 test picking with partial pieces and other peers present so that both
// backup_pieces and backup_pieces2 are used
