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

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <iostream>

#include "test.hpp"

using namespace libtorrent;

const int blocks_per_piece = 4;

bitfield string2vec(char const* have_str)
{
	const int num_pieces = strlen(have_str);
	bitfield have(num_pieces, false);
	for (int i = 0; i < num_pieces; ++i)
		if (have_str[i] != ' ') have.set_bit(i);
	return have;
}

ipv4_peer* tmp_peer = 0;

tcp::endpoint endp;
ipv4_peer tmp0(endp, false, 0);
ipv4_peer tmp1(endp, false, 0);
ipv4_peer tmp2(endp, false, 0);
ipv4_peer tmp3(endp, false, 0);
ipv4_peer tmp4(endp, false, 0);
ipv4_peer tmp5(endp, false, 0);
ipv4_peer tmp6(endp, false, 0);
ipv4_peer tmp7(endp, false, 0);
ipv4_peer tmp8(endp, false, 0);
ipv4_peer tmp9(endp, false, 0);
ipv4_peer peer_struct(endp, true, 0);

const std::vector<int> empty_vector;

// availability is a string where each character is the
// availability of that piece, '1', '2' etc.
// have_str is a string where each character represents a
// piece, ' ' means we don't have the piece and any other
// character means we have it
boost::shared_ptr<piece_picker> setup_picker(
	char const* availability
	, char const* have_str
	, char const* priority
	, char const* partial)
{
	const int num_pieces = strlen(availability);
	TORRENT_ASSERT(int(strlen(have_str)) == num_pieces);

	boost::shared_ptr<piece_picker> p(new piece_picker);
	p->init(blocks_per_piece, blocks_per_piece, num_pieces);

	for (int i = 0; i < num_pieces; ++i)
	{
		const int avail = availability[i] - '0';
		assert(avail >= 0);
		
		static const torrent_peer* peers[10] = { &tmp0, &tmp1, &tmp2
			, &tmp3, &tmp4, &tmp5, &tmp6, &tmp7, &tmp8, &tmp9 };
		TORRENT_ASSERT(avail < 10);
		for (int j = 0; j < avail; ++j) p->inc_refcount(i, peers[j]);
	}

	bitfield have = string2vec(have_str);

	for (int i = 0; i < num_pieces; ++i)
	{
		if (partial[i] == 0) break;

		if (partial[i] == ' ') continue;

		int blocks = 0;
		if (partial[i] >= '0' && partial[i] <= '9')
			blocks = partial[i] - '0';
		else
			blocks = partial[i] - 'a' + 10;

		int counter = 0;
		for (int j = 0; j < 4; ++j)
		{
			TEST_CHECK(!p->is_finished(piece_block(i, j)));
			if ((blocks & (1 << j)) == 0) continue;
			++counter;
			bool ret = p->mark_as_downloading(piece_block(i, j), (void*)tmp_peer);
			TEST_CHECK(ret == true);
			TEST_CHECK(p->is_requested(piece_block(i, j)) == bool(blocks & (1 << j)));
			p->mark_as_writing(piece_block(i, j), (void*)tmp_peer);
			TEST_CHECK(!p->is_finished(piece_block(i, j)));
			// trying to mark a block as requested after it has been completed
			// should fail (return false)
			ret = p->mark_as_downloading(piece_block(i, j), (void*)tmp_peer);
			TEST_CHECK(ret == false);
			p->mark_as_finished(piece_block(i, j), (void*)tmp_peer);

			TEST_CHECK(p->is_downloaded(piece_block(i, j)) == bool(blocks & (1 << j)));
			TEST_CHECK(p->is_finished(piece_block(i, j)) == bool(blocks & (1 << j)));
		}

		piece_picker::downloading_piece st;
		p->piece_info(i, st);
		TEST_CHECK(st.writing == 0);
		TEST_CHECK(st.requested == 0);
		TEST_CHECK(st.index == i);

		TEST_CHECK(st.finished == counter);
		TEST_CHECK(st.finished + st.requested + st.writing == counter);

		TEST_CHECK(p->is_piece_finished(i) == (counter == 4));
	}

	for (int i = 0; i < num_pieces; ++i)
	{
		if (priority[i] == 0) break;
		const int prio = priority[i] - '0';
		assert(prio >= 0);
		p->set_piece_priority(i, prio);

		TEST_CHECK(p->piece_priority(i) == prio);
	}

	for (int i = 0; i < num_pieces; ++i)
	{
		if (!have[i]) continue;
		p->we_have(i);
		for (int j = 0; j < blocks_per_piece; ++j)
			TEST_CHECK(p->is_finished(piece_block(i, j)));
	}

	std::vector<int> availability_vec;
	p->get_availability(availability_vec);
	for (int i = 0; i < num_pieces; ++i)
	{
		const int avail = availability[i] - '0';
		assert(avail >= 0);
		TEST_CHECK(avail == availability_vec[i]);
	}

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
	p->check_invariant();
#endif
	return p;
}

bool verify_pick(boost::shared_ptr<piece_picker> p
	, std::vector<piece_block> const& picked, bool allow_multi_blocks = false)
{
#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
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
		, std::insert_iterator<std::set<piece_block> >(blocks, blocks.end()));
	std::cerr << " verify: " << picked.size() << " " << blocks.size() << std::endl;
	return picked.size() == blocks.size();
}

void print_availability(boost::shared_ptr<piece_picker> const& p)
{
	std::vector<int> avail;
	p->get_availability(avail);
	printf("[ ");
	for (std::vector<int>::iterator i = avail.begin()
		, end(avail.end()); i != end; ++i)
	{
		printf("%d ", *i);
	}
	printf("]\n");
}

bool verify_availability(boost::shared_ptr<piece_picker> const& p, char const* a)
{
	std::vector<int> avail;
	p->get_availability(avail);
	for (std::vector<int>::iterator i = avail.begin()
		, end(avail.end()); i != end; ++i, ++a)
	{
		if (*a - '0' != *i) return false;
	}
	return true;
}

void print_pick(std::vector<piece_block> const& picked)
{
	for (int i = 0; i < int(picked.size()); ++i)
	{
		std::cout << "(" << picked[i].piece_index << ", " << picked[i].block_index << ") ";
	}
	std::cout << std::endl;
}

void print_title(char const* name)
{
	std::cerr << "==== " << name << " ====\n";
}

std::vector<piece_block> pick_pieces(boost::shared_ptr<piece_picker> const& p
	, char const* availability
	, int num_blocks
	, int prefer_contiguous_blocks
	, void* peer_struct
	, int options = piece_picker::rarest_first
	, std::vector<int> const& suggested_pieces = empty_vector)
{
	std::vector<piece_block> picked;
	counters pc;
	p->pick_pieces(string2vec(availability), picked
		, num_blocks, prefer_contiguous_blocks, peer_struct
		, options, suggested_pieces, 20, pc);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	return picked;
}

int test_pick(boost::shared_ptr<piece_picker> const& p
	, int options = piece_picker::rarest_first)
{
	const std::vector<int> empty_vector;
	std::vector<piece_block> picked = pick_pieces(p, "*******", 1, 0, 0
		, options, empty_vector);
	if (picked.empty()) return -1;
	return picked[0].piece_index;
}

int test_main()
{
	tcp::endpoint endp;
	piece_picker::downloading_piece st;
#if TORRENT_USE_ASSERTS
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
#endif
	tmp_peer = &tmp1;
	std::vector<piece_block> picked;
	boost::shared_ptr<piece_picker> p;
	const int options = piece_picker::rarest_first;
	std::pair<int, int> dc;
	counters pc;

	print_title("test piece_block");

	TEST_CHECK(piece_block(0, 0) != piece_block(0, 1));
	TEST_CHECK(piece_block(0, 0) != piece_block(1, 0));
	TEST_CHECK(!(piece_block(0, 0) != piece_block(0, 0)));

	TEST_CHECK(!(piece_block(0, 0) == piece_block(0, 1)));
	TEST_CHECK(!(piece_block(0, 0) == piece_block(1, 0)));
	TEST_CHECK(piece_block(0, 0) == piece_block(0, 0));

	TEST_CHECK(!(piece_block(0, 1) < piece_block(0, 0)));
	TEST_CHECK(!(piece_block(1, 0) < piece_block(0, 0)));
	TEST_CHECK(piece_block(0, 0) < piece_block(0, 1));
	TEST_CHECK(piece_block(0, 0) < piece_block(1, 0));
	TEST_CHECK(!(piece_block(0, 0) < piece_block(0, 0)));
	TEST_CHECK(!(piece_block(1, 0) < piece_block(1, 0)));
	TEST_CHECK(!(piece_block(0, 1) < piece_block(0, 1)));

// ========================================================

	// test abort_download
	print_title("test abort_download");

	p = setup_picker("1111111", "       ", "7110000", ""); 
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) != picked.end());

	p->abort_download(piece_block(0,0), tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) != picked.end());

	p->mark_as_downloading(piece_block(0,0), &tmp1);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == true);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) == picked.end());

	p->abort_download(piece_block(0,0), tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) != picked.end());

	p->mark_as_downloading(piece_block(0,0), &tmp1);
	p->mark_as_downloading(piece_block(0,1), &tmp1);
	p->abort_download(piece_block(0,0), tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) != picked.end());

	p->mark_as_downloading(piece_block(0,0), &tmp1);
	p->mark_as_writing(piece_block(0,0), &tmp1);
	p->write_failed(piece_block(0,0));
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(1,0)) != picked.end()
		|| std::find(picked.begin(), picked.end(), piece_block(2,0)) != picked.end());
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) == picked.end());
	p->restore_piece(0);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) != picked.end());

	p->mark_as_downloading(piece_block(0,0), &tmp1);
	p->mark_as_writing(piece_block(0,0), &tmp1);
	p->mark_as_finished(piece_block(0,0), &tmp1);
	p->abort_download(piece_block(0,0), tmp_peer);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, tmp_peer
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) == picked.end());

// ========================================================

	print_title("test abort_download");

	p = setup_picker("1111111", "       ", "7110000", ""); 
	p->mark_as_downloading(piece_block(0,0), &tmp1);
	p->mark_as_finished(piece_block(0,1), 0);
	p->piece_info(0, st);
	TEST_EQUAL(st.requested, 1);
	TEST_EQUAL(st.finished, 1);
	p->abort_download(piece_block(0,0), tmp_peer);
	p->piece_info(0, st);
	TEST_EQUAL(st.requested, 0);
	TEST_EQUAL(st.finished, 1);
	picked = pick_pieces(p, "*******", blocks_per_piece, 0, 0
		, options, empty_vector);
	TEST_CHECK(p->is_requested(piece_block(0, 0)) == false);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(0,0)) != picked.end());

// ========================================================

	print_title("test get_downloaders");

	p = setup_picker("1111111", "       ", "7110000", ""); 

	p->mark_as_downloading(piece_block(0, 2), &tmp1);
	p->mark_as_writing(piece_block(0, 2), &tmp1);
	p->abort_download(piece_block(0, 2), &tmp1);
	p->mark_as_downloading(piece_block(0, 2), &tmp2);
	p->mark_as_writing(piece_block(0, 2), &tmp2);

	std::vector<void*> d;
	p->get_downloaders(d, 0);
	TEST_EQUAL(d.size(), 4);
	TEST_CHECK(d[0] == NULL);
	TEST_CHECK(d[1] == NULL);
	TEST_CHECK(d[2] == &tmp2);
	TEST_CHECK(d[3] == NULL);

	p->mark_as_downloading(piece_block(0, 3), &tmp1);
	p->abort_download(piece_block(0, 3), &tmp1);
	p->mark_as_downloading(piece_block(0, 3), &tmp2);
	p->mark_as_writing(piece_block(0, 3), &tmp2);

	p->get_downloaders(d, 0);

	TEST_EQUAL(d.size(), 4);
	TEST_CHECK(d[0] == NULL);
	TEST_CHECK(d[1] == NULL);
	TEST_CHECK(d[2] == &tmp2);
	TEST_CHECK(d[3] == &tmp2);

	// if we ask for downloaders for a piece that's not
	// curently being downloaded, we get zeroes back
	p->get_downloaders(d, 1);

	TEST_EQUAL(d.size(), 4);
	TEST_CHECK(d[0] == NULL);
	TEST_CHECK(d[1] == NULL);
	TEST_CHECK(d[2] == NULL);
	TEST_CHECK(d[3] == NULL);

// ========================================================

	p = setup_picker("2222", "    ", "", "");

	for (int i = 0; i < 4; ++i)
		for (int k = 0; k < blocks_per_piece; ++k)
			p->mark_as_downloading(piece_block(i, k), &tmp1);

	p->mark_as_downloading(piece_block(0, 0), &tmp2);

	fprintf(stderr, "num_peers: %d\n", p->num_peers(piece_block(0, 0)));
	TEST_EQUAL(p->num_peers(piece_block(0, 0)), 2);

	p->abort_download(piece_block(0, 0), &tmp1);

	fprintf(stderr, "num_peers: %d\n", p->num_peers(piece_block(0, 0)));
	TEST_EQUAL(p->num_peers(piece_block(0, 0)), 1);

// ========================================================

	// make sure the block that is picked is from piece 1, since it
	// it is the piece with the lowest availability
	print_title("test pick lowest availability");
	p = setup_picker("2223333", "* * *  ", "", "");
	TEST_CHECK(test_pick(p) == 1);
	
// ========================================================

	// make sure pieces with equal priority and availability
	// are picked at random
	print_title("test random pick at same priority");
	std::map<int, int> random_prio_pieces;
	for (int i = 0; i < 100; ++i)
	{
		p = setup_picker("1111112", "       ", "", "");
		++random_prio_pieces[test_pick(p)];
	}
	TEST_CHECK(random_prio_pieces.size() == 6);
	for (std::map<int, int>::iterator i = random_prio_pieces.begin()
		, end(random_prio_pieces.end()); i != end; ++i)
		std::cout << i->first << ": " << i->second << " ";
	std::cout << std::endl;

// ========================================================

	// make sure the block that is picked is from piece 5, since it
	// has the highest priority among the available pieces
	print_title("test pick highest priority");
	p = setup_picker("1111111", "       ", "1111121", "");
	TEST_CHECK(test_pick(p) == 5);

	p = setup_picker("1111111", "       ", "1171121", "");
	TEST_CHECK(test_pick(p) == 2);

	p = setup_picker("1111111", "       ", "1131521", "");
	TEST_CHECK(test_pick(p) == 4);

// ========================================================

	print_title("test reverse rarest first");
	p = setup_picker("4179253", "       ", "", "");
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, &peer_struct
		, piece_picker::rarest_first | piece_picker::reverse, empty_vector);
	int expected_common_pieces[] = {3, 2, 5, 0, 6, 4, 1};
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(expected_common_pieces[i / blocks_per_piece], i % blocks_per_piece));

	// piece 3 should NOT be prioritized since it's a partial, and not
	// reversed. Reversed partials are considered reversed
	p = setup_picker("1122111", "       ", "3333333", "   1   ");
	TEST_CHECK(test_pick(p, piece_picker::rarest_first | piece_picker::reverse) == 2);

// ========================================================

	// make sure the 4 blocks are picked from the same piece if
	// whole pieces are preferred. Priority and availability is more
	// important. Piece 1 has the lowest availability even though
	// it is not a whole piece
	print_title("test pick whole pieces");
	p = setup_picker("2212222", "       ", "1111111", "1023460");
	picked = pick_pieces(p, "****** ", 1, blocks_per_piece
		, &peer_struct, options, empty_vector);
	TEST_EQUAL(int(picked.size()), 3);
	for (int i = 0; i < blocks_per_piece && i < int(picked.size()); ++i)
		TEST_EQUAL(picked[i].piece_index, 2);

	p = setup_picker("1111111", "       ", "1111111", "");
	picked = pick_pieces(p, "****** ", 1, blocks_per_piece
		, &peer_struct, options, empty_vector);
	TEST_EQUAL(int(picked.size()), blocks_per_piece);
	for (int i = 0; i < blocks_per_piece && i < int(picked.size()); ++i)
		TEST_EQUAL(picked[i].block_index, i);

	p = setup_picker("2221222", "       ", "", "");
	picked = pick_pieces(p, "*******", 1, 7 * blocks_per_piece
		, &peer_struct, options, empty_vector);
	TEST_EQUAL(int(picked.size()), 7 * blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(i / blocks_per_piece, i % blocks_per_piece));

// ========================================================

	// test the distributed copies function. It should include ourself
	// in the availability. i.e. piece 0 has availability 2.
	// there are 2 pieces with availability 2 and 5 with availability 3
	print_title("test distributed copies");
	p = setup_picker("1233333", "*      ", "", "");
	dc = p->distributed_copies();
	TEST_CHECK(dc == std::make_pair(2, 5000 / 7));

// ========================================================
	
	// make sure filtered pieces are ignored
	print_title("test filtered pieces");
	p = setup_picker("1111111", "       ", "0010000", "");
	TEST_CHECK(test_pick(p, piece_picker::rarest_first) == 2);
	TEST_CHECK(test_pick(p, piece_picker::rarest_first | piece_picker::reverse) == 2);
	TEST_CHECK(test_pick(p, piece_picker::sequential) == 2);
	TEST_CHECK(test_pick(p, piece_picker::sequential | piece_picker::reverse) == 2);

// ========================================================
	
	// make sure we_dont_have works
	print_title("test we_dont_have");
	p = setup_picker("1111111", "*******", "0100000", "");
	TEST_CHECK(p->have_piece(1));
	TEST_CHECK(p->have_piece(2));
	p->we_dont_have(1);
	p->we_dont_have(2);
	TEST_CHECK(!p->have_piece(1));
	TEST_CHECK(!p->have_piece(2));
	picked = pick_pieces(p, "*** ** ", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == 1);

// ========================================================

	// make sure we can split m_seed when removing a refcount
	print_title("test dec_refcount split seed");
	p = setup_picker("0000000", "       ", "0000000", "");
	p->inc_refcount_all(0);

	std::vector<int> avail;
	p->get_availability(avail);
	TEST_EQUAL(avail.size(), 7);
	TEST_CHECK(avail[0] != 0);
	TEST_CHECK(avail[1] != 0);
	TEST_CHECK(avail[2] != 0);
	TEST_CHECK(avail[3] != 0);
	TEST_CHECK(avail[4] != 0);

	p->dec_refcount(3, 0);

	p->get_availability(avail);
	TEST_EQUAL(avail.size(), 7);

	TEST_CHECK(avail[0] != 0);
	TEST_CHECK(avail[1] != 0);
	TEST_CHECK(avail[2] != 0);
	TEST_CHECK(avail[3] == 0);
	TEST_CHECK(avail[4] != 0);

// ========================================================
	
	// make sure init preserves priorities
	print_title("test init");
	p = setup_picker("1111111", "       ", "1111111", "");

	TEST_CHECK(p->num_filtered() == 0);
	TEST_CHECK(p->num_have_filtered() == 0);
	TEST_CHECK(p->num_have() == 0);

	p->set_piece_priority(0, 0);
	TEST_CHECK(p->num_filtered() == 1);
	TEST_CHECK(p->num_have_filtered() == 0);
	TEST_CHECK(p->num_have() == 0);
	
	p->we_have(0);

	TEST_CHECK(p->num_filtered() == 0);
	TEST_CHECK(p->num_have_filtered() == 1);
	TEST_CHECK(p->num_have() == 1);

	p->init(blocks_per_piece, blocks_per_piece, blocks_per_piece * 7);
	TEST_CHECK(p->piece_priority(0) == 0);
	TEST_CHECK(p->num_filtered() == 1);
	TEST_CHECK(p->num_have_filtered() == 0);
	TEST_CHECK(p->num_have() == 0);

// ========================================================
	
	// make sure requested blocks aren't picked
	print_title("test don't pick requested blocks");
	p = setup_picker("1111111", "       ", "", "");
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	piece_block first = picked.front();
	p->mark_as_downloading(picked.front(), &peer_struct);
	TEST_CHECK(p->num_peers(picked.front()) == 1);
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() != first);

// ========================================================

	// make sure downloading pieces have higher priority
	print_title("test downloading piece priority");
	p = setup_picker("1111111", "       ", "", "");
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	first = picked.front();
	p->mark_as_downloading(picked.front(), &peer_struct);
	TEST_CHECK(p->num_peers(picked.front()) == 1);
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() != first);
	TEST_CHECK(picked.front().piece_index == first.piece_index);

// ========================================================

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
	print_title("test partial piece order (rarest first)");
	p = setup_picker("1112111", "       ", "", "013700f");
	picked = pick_pieces(p, "*******", 1, 0, 0
		, options | piece_picker::prioritize_partials, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(2, 2)
		|| picked.front() == piece_block(2, 3));

	// as a tie breaker, make sure downloading pieces closer to completion have
	// higher priority. piece 3 is only 1 block from being completed, and should
	// be picked

	print_title("test partial piece order (most complete)");
	p = setup_picker("1111111", "       ", "", "013700f");
	picked = pick_pieces(p, "*******", 1, 0, 0
		, options | piece_picker::prioritize_partials, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(3, 3));

	// if we don't use rarest first when we prioritize partials, but instead use
	// sequential order, make sure we pick the right one

	print_title("test partial piece order (sequential)");
	p = setup_picker("1111111", "       ", "", "013700f");
	picked = pick_pieces(p, "*******", 1, 0, 0
		, piece_picker::sequential | piece_picker::prioritize_partials, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(1, 1)
		|| picked.front() == piece_block(1, 2)
		|| picked.front() == piece_block(1, 3));

// ========================================================

	// make sure the random piece picker can still pick partial pieces
	print_title("test random picking (downloading piece)");
	p = setup_picker("1111111", "       ", "", "013700f");
	picked = pick_pieces(p, " ***  *", 1, 0, 0
		, 0, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(1, 1)
		|| picked.front() == piece_block(2, 2)
		|| picked.front() == piece_block(3, 3));

	// make sure the random piece picker can still pick partial pieces
	// even when prefer_contiguous_blocks is set
	print_title("test random picking (downloading piece, prefer contiguous)");
	p = setup_picker("1111111", "       ", "", "013700f");
	picked = pick_pieces(p, " ***  *", 1, 4, 0
		, 0, empty_vector);
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() == piece_block(1, 1)
		|| picked.front() == piece_block(2, 2)
		|| picked.front() == piece_block(3, 3));


// ========================================================

	// test sequential download
	print_title("test sequential download");
	p = setup_picker("7654321", "       ", "", "");
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, 0
		, piece_picker::sequential, empty_vector);
	TEST_CHECK(int(picked.size()) == 7 * blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(i / blocks_per_piece, i % blocks_per_piece));

// ========================================================

	// test reverse sequential download
	print_title("test reverse sequential download");
	p = setup_picker("7654321", "       ", "", "");
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, 0
		, piece_picker::sequential | piece_picker::reverse, empty_vector);
	TEST_CHECK(int(picked.size()) == 7 * blocks_per_piece);
	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(6 - (i / blocks_per_piece), i % blocks_per_piece));

// ========================================================

	// test priority sequential download
	print_title("test priority sequential download");
	p = setup_picker("7654321", "       ", "1117071", "");
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, 0
		, piece_picker::sequential, empty_vector);

	// the piece with priority 0 was not picked, everything else should
	// be picked
	TEST_EQUAL(int(picked.size()), 6 * blocks_per_piece);

	// the first two pieces picked should be 3 and 5 since those have priority 7
	for (int i = 0; i < 2 * blocks_per_piece; ++i)
		TEST_CHECK(picked[i].piece_index == 3 || picked[i].piece_index == 5);

	int expected[] = {-1, -1, 0, 1, 2, 6};
	for (int i = 2 * blocks_per_piece; i < int(picked.size()); ++i)
		TEST_EQUAL(picked[i].piece_index, expected[i / blocks_per_piece]);

// ========================================================

	// sweep up, we_have()
	print_title("test cursors. sweep up, we_have");
	p = setup_picker("7654321", "       ", "", "");
	for (int i = 0; i < 7; ++i)
	{
		TEST_EQUAL(p->cursor(), i);
		TEST_EQUAL(p->reverse_cursor(), 7);
		p->we_have(i);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);

	// sweep up, set_piece_priority()
	print_title("test cursors. sweep up, set_piece_priority");
	p = setup_picker("7654321", "       ", "", "");
	for (int i = 0; i < 7; ++i)
	{
		TEST_EQUAL(p->cursor(), i);
		TEST_EQUAL(p->reverse_cursor(), 7);
		p->set_piece_priority(i, 0);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);

	// sweep down, we_have()
	print_title("test cursors. sweep down, we_have");
	p = setup_picker("7654321", "       ", "", "");
	for (int i = 6; i >= 0; --i)
	{
		TEST_EQUAL(p->cursor(), 0);
		TEST_EQUAL(p->reverse_cursor(), i + 1);
		p->we_have(i);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);

	// sweep down, set_piece_priority()
	print_title("test cursors. sweep down, set_piece_priority");
	p = setup_picker("7654321", "       ", "", "");
	for (int i = 6; i >= 0; --i)
	{
		TEST_EQUAL(p->cursor(), 0);
		TEST_EQUAL(p->reverse_cursor(), i + 1);
		p->set_piece_priority(i, 0);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);

	// sweep in, set_piece_priority()
	print_title("test cursors. sweep in, set_piece_priority");
	p = setup_picker("7654321", "       ", "", "");
	for (int left = 0, right = 6; left <= 3 && right >= 3; ++left, --right)
	{
		TEST_EQUAL(p->cursor(), left);
		TEST_EQUAL(p->reverse_cursor(), right + 1);
		p->set_piece_priority(left, 0);
		p->set_piece_priority(right, 0);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);

	// sweep in, we_have()
	print_title("test cursors. sweep in, we_have");
	p = setup_picker("7654321", "       ", "", "");
	for (int left = 0, right = 6; left <= 3 && right >= 3; ++left, --right)
	{
		TEST_EQUAL(p->cursor(), left);
		TEST_EQUAL(p->reverse_cursor(), right + 1);
		p->we_have(left);
		p->we_have(right);
	}
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);


	print_title("test cursors. sweep up, we_dont_have");
	p = setup_picker("7654321", "*******", "", "");
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);
	for (int i = 0; i < 7; ++i)
	{
		p->we_dont_have(i);
		TEST_EQUAL(p->cursor(), 0);
		TEST_EQUAL(p->reverse_cursor(), i + 1);
	}
	TEST_CHECK(!p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), 0);
	TEST_EQUAL(p->reverse_cursor(), 7);

	print_title("test cursors. sweep down, we_dont_have");
	p = setup_picker("7654321", "*******", "", "");
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);
	for (int i = 6; i >= 0; --i)
	{
		p->we_dont_have(i);
		TEST_EQUAL(p->cursor(), i);
		TEST_EQUAL(p->reverse_cursor(), 7);
	}
	TEST_CHECK(!p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), 0);
	TEST_EQUAL(p->reverse_cursor(), 7);

	print_title("test cursors. sweep out, we_dont_have");
	p = setup_picker("7654321", "*******", "", "");
	TEST_CHECK(p->is_finished());
	TEST_CHECK(p->is_seeding());
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);
	for (int left = 3, right = 3; left >= 0 && right < 7; --left, ++right)
	{
		p->we_dont_have(left);
		p->we_dont_have(right);
		TEST_EQUAL(p->cursor(), left);
		TEST_EQUAL(p->reverse_cursor(), right + 1);
	}
	TEST_CHECK(!p->is_finished());
	TEST_CHECK(!p->is_seeding());
	TEST_EQUAL(p->cursor(), 0);
	TEST_EQUAL(p->reverse_cursor(), 7);

	// test cursors
	print_title("test cursors");
	p = setup_picker("7654321", "       ", "", "");
	TEST_EQUAL(p->cursor(), 0);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->we_have(1);
	TEST_EQUAL(p->cursor(), 0);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->we_have(0);
	TEST_EQUAL(p->cursor(), 2);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->we_have(5);
	TEST_EQUAL(p->cursor(), 2);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->we_have(6);
	TEST_EQUAL(p->cursor(), 2);
	TEST_EQUAL(p->reverse_cursor(), 5);
	p->we_have(4);
	p->we_have(3);
	p->we_have(2);
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);

	p = setup_picker("7654321", "       ", "", "");
	TEST_EQUAL(p->cursor() ,  0);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->set_piece_priority(1, 0);
	TEST_EQUAL(p->cursor(), 0);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->set_piece_priority(0, 0);
	TEST_EQUAL(p->cursor(), 2);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->set_piece_priority(5, 0);
	TEST_EQUAL(p->cursor(), 2);
	TEST_EQUAL(p->reverse_cursor(), 7);
	p->set_piece_priority(6, 0);
	TEST_EQUAL(p->cursor(), 2);
	TEST_EQUAL(p->reverse_cursor(), 5);
	p->set_piece_priority(4, 0);
	p->set_piece_priority(3, 0);
	p->set_piece_priority(2, 0);
	TEST_EQUAL(p->cursor(), 7);
	TEST_EQUAL(p->reverse_cursor(), 0);
	p->set_piece_priority(3, 1);
	TEST_EQUAL(p->cursor(), 3);
	TEST_EQUAL(p->reverse_cursor(), 4);

// ========================================================

	// test piece priorities
	print_title("test piece priorities");
	p = setup_picker("5555555", "       ", "7654321", "");
	TEST_CHECK(p->num_filtered() == 0);
	TEST_CHECK(p->num_have_filtered() == 0);
	p->set_piece_priority(0, 0);
	TEST_CHECK(p->num_filtered() == 1);
	TEST_CHECK(p->num_have_filtered() == 0);
	p->mark_as_finished(piece_block(0,0), 0);
	p->we_have(0);
	TEST_CHECK(p->num_filtered() == 0);
	TEST_CHECK(p->num_have_filtered() == 1);
	
	p->we_dont_have(0);
	p->set_piece_priority(0, 7);

	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, 0
		, options, empty_vector);
	TEST_CHECK(int(picked.size()) == 7 * blocks_per_piece);

	for (int i = 0; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(i / blocks_per_piece, i % blocks_per_piece));
	
	// test changing priority on a piece we have
	p->we_have(0);
	p->set_piece_priority(0, 0);
	p->set_piece_priority(0, 1);
	p->set_piece_priority(0, 0);

	std::vector<int> prios;
	p->piece_priorities(prios);
	TEST_CHECK(prios.size() == 7);
	int prio_comp[] = {0, 6, 5, 4, 3, 2, 1};
	TEST_CHECK(std::equal(prios.begin(), prios.end(), prio_comp));
	
	std::vector<bool> filter;
	p->filtered_pieces(filter);
	TEST_CHECK(prios.size() == 7);
	bool filter_comp[] = {true, false, false, false, false, false, false};
	TEST_CHECK(std::equal(filter.begin(), filter.end(), filter_comp));

// ========================================================

	// test restore_piece
	print_title("test restore piece");
	p = setup_picker("1234567", "       ", "", "");
	p->mark_as_finished(piece_block(0,0), 0);
	p->mark_as_finished(piece_block(0,1), 0);
	p->mark_as_finished(piece_block(0,2), 0);
	p->mark_as_finished(piece_block(0,3), 0);

	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 1);

	p->restore_piece(0);	
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 0);

	p->mark_as_finished(piece_block(0,0), 0);
	p->mark_as_finished(piece_block(0,1), 0);
	p->mark_as_finished(piece_block(0,2), 0);
	p->mark_as_finished(piece_block(0,3), 0);
	p->set_piece_priority(0, 0);

	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 1);

	p->restore_piece(0);	
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 1);

	p->set_piece_priority(0, 7);
	picked = pick_pieces(p, "*******", 1, 0, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 0);

// ========================================================

	// test random mode
	print_title("test random pick");
	p = setup_picker("1234567", "       ", "1111122", "");
	std::set<int> random_pieces;
	for (int i = 0; i < 100; ++i)
		random_pieces.insert(test_pick(p, 0));
	TEST_CHECK(random_pieces.size() == 7);

	random_pieces.clear();
	for (int i = 0; i < 7; ++i)
	{
		int piece = test_pick(p, 0);
		p->we_have(piece);
		random_pieces.insert(piece);
	}
	TEST_CHECK(random_pieces.size() == 7);

// ========================================================

	// make sure the piece picker will pick pieces that
	// are already requested from other peers if it has to
	print_title("test picking downloading blocks");
	p = setup_picker("1111111", "       ", "", "");
	p->mark_as_downloading(piece_block(2,2), &tmp1);
	p->mark_as_downloading(piece_block(1,2), &tmp1);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 7 * blocks_per_piece, 0, 0
		, piece_picker::prioritize_partials, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// don't pick both busy pieces, if there are already other blocks picked
	TEST_EQUAL(picked.size(), 7 * blocks_per_piece - 2);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 7 * blocks_per_piece, 0, 0
		, piece_picker::prioritize_partials
		| piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// don't pick both busy pieces, if there are already other blocks picked
	TEST_EQUAL(picked.size(), 7 * blocks_per_piece - 2);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 7 * blocks_per_piece, 0, 0
		, piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// don't pick both busy pieces, if there are already other blocks picked
	TEST_EQUAL(picked.size(), 7 * blocks_per_piece - 2);

	// make sure we still pick from a partial piece even when prefering whole pieces
	picked.clear();
	p->pick_pieces(string2vec(" *     "), picked, 1, blocks_per_piece, 0
		, piece_picker::rarest_first
			| piece_picker::align_expanded_pieces, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);
	TEST_CHECK(picked.size() >= 1 && picked[0].piece_index == 1);

	// don't pick locked pieces
	picked.clear();
	p->lock_piece(1);
	p->pick_pieces(string2vec(" **    "), picked, 7, 0, 0
		, piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 3);
	TEST_CHECK(picked.size() >= 1 && picked[0].piece_index == 2);

	p->restore_piece(1);
	p->mark_as_downloading(piece_block(2,0), &tmp1);
	p->mark_as_downloading(piece_block(2,1), &tmp1);
	p->mark_as_downloading(piece_block(2,3), &tmp1);
	p->mark_as_downloading(piece_block(1,0), &tmp1);
	p->mark_as_downloading(piece_block(1,1), &tmp1);
	p->mark_as_downloading(piece_block(1,2), &tmp1);
	p->mark_as_downloading(piece_block(1,3), &tmp1);

	picked.clear();
	p->pick_pieces(string2vec(" **    "), picked, 2, 0, 0
		, piece_picker::rarest_first, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);

	picked.clear();
	p->pick_pieces(string2vec(" **    "), picked, 2 * blocks_per_piece, 0, 0
		, piece_picker::prioritize_partials, empty_vector, 0
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);

	picked.clear();
	p->pick_pieces(string2vec(" **    "), picked, 2 * blocks_per_piece, 0, 0
		, piece_picker::prioritize_partials, empty_vector, 20
		, pc);
	TEST_CHECK(verify_pick(p, picked, true));
	print_pick(picked);
	// always only pick one busy piece
	TEST_EQUAL(picked.size(), 1);

// ========================================================
	
	// test clear_peer
	print_title("test clear_peer");
	p = setup_picker("1123333", "       ", "", "");
	p->mark_as_downloading(piece_block(0, 0), &tmp1);
	p->mark_as_downloading(piece_block(0, 1), &tmp2);
	p->mark_as_downloading(piece_block(0, 2), &tmp3);
	p->mark_as_downloading(piece_block(1, 1), &tmp1);
	p->mark_as_downloading(piece_block(2, 1), &tmp2);
	p->mark_as_downloading(piece_block(3, 1), &tmp3);

	std::vector<void*> dls;
	void* expected_dls1[] = {&tmp1, &tmp2, &tmp3, 0};
	void* expected_dls2[] = {0, &tmp1, 0, 0};
	void* expected_dls3[] = {0, &tmp2, 0, 0};
	void* expected_dls4[] = {0, &tmp3, 0, 0};
	void* expected_dls5[] = {&tmp1, 0, &tmp3, 0};
	p->get_downloaders(dls, 0);
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls1));
	p->get_downloaders(dls, 1);
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls2));
	p->get_downloaders(dls, 2);
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls3));
	p->get_downloaders(dls, 3);
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls4));

	p->clear_peer(&tmp2);
	p->get_downloaders(dls, 0);
	TEST_CHECK(std::equal(dls.begin(), dls.end(), expected_dls5));

// ========================================================
	
	// test have_all and have_none
	print_title("test have_all and have_none");
	p = setup_picker("0123333", "*      ", "", "");
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(1, 5000 / 7));
	p->inc_refcount_all(&tmp8);
	dc = p->distributed_copies();
	TEST_CHECK(dc == std::make_pair(2, 5000 / 7));
	p->dec_refcount_all(&tmp8);
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(1, 5000 / 7));
	p->inc_refcount(0, &tmp0);
	p->dec_refcount_all(&tmp0);
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(0, 6000 / 7));
	TEST_CHECK(test_pick(p) == 2);

// ========================================================
	
	// test have_all and have_none
	print_title("test have_all and have_none with sequential download");
	p = setup_picker("0123333", "*      ", "", "");
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(1, 5000 / 7));
	p->inc_refcount_all(&tmp8);
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc.first << "." << (dc.second / 1000.f) << std::endl;
	TEST_CHECK(dc == std::make_pair(2, 5000 / 7));
	TEST_CHECK(test_pick(p) == 1);

// ========================================================

	// test inc_ref and dec_ref
	print_title("test inc_ref dec_ref");
	p = setup_picker("1233333", "     * ", "", "");
	TEST_CHECK(test_pick(p) == 0);

	p->dec_refcount(0, &tmp0);
	TEST_CHECK(test_pick(p) == 1);

	p->dec_refcount(4, &tmp0);
	p->dec_refcount(4, &tmp1);
	TEST_CHECK(test_pick(p) == 4);

	// decrease refcount on something that's not in the piece list
	p->dec_refcount(5, &tmp0);
	p->inc_refcount(5, &tmp0);
	
	bitfield bits = string2vec("*      ");
	TEST_EQUAL(bits.get_bit(0), true);
	TEST_EQUAL(bits.get_bit(1), false);
	TEST_EQUAL(bits.get_bit(2), false);
	TEST_EQUAL(bits.get_bit(3), false);
	TEST_EQUAL(bits.get_bit(4), false);
	TEST_EQUAL(bits.get_bit(5), false);
	TEST_EQUAL(bits.get_bit(6), false);
	p->inc_refcount(bits, &tmp0);
	bits = string2vec("    *  ");

	TEST_EQUAL(bits.get_bit(0), false);
	TEST_EQUAL(bits.get_bit(1), false);
	TEST_EQUAL(bits.get_bit(2), false);
	TEST_EQUAL(bits.get_bit(3), false);
	TEST_EQUAL(bits.get_bit(4), true);
	TEST_EQUAL(bits.get_bit(5), false);
	TEST_EQUAL(bits.get_bit(6), false);
	p->dec_refcount(bits, &tmp2);
	TEST_EQUAL(test_pick(p), 0);

// ========================================================
	
	// test unverified_blocks, marking blocks and get_downloader
	print_title("test unverified blocks");
	p = setup_picker("1111111", "       ", "", "0300700");
	TEST_CHECK(p->unverified_blocks() == 2 + 3);
	TEST_CHECK(p->get_downloader(piece_block(4, 0)) == (void*)tmp_peer);
	TEST_CHECK(p->get_downloader(piece_block(4, 1)) == (void*)tmp_peer);
	TEST_CHECK(p->get_downloader(piece_block(4, 2)) == (void*)tmp_peer);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == 0);
	p->mark_as_downloading(piece_block(4, 3), &peer_struct);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == &peer_struct);
	p->piece_info(4, st);
	TEST_CHECK(st.requested == 1);
	TEST_CHECK(st.writing == 0);
	TEST_CHECK(st.finished == 3);
	TEST_CHECK(p->unverified_blocks() == 2 + 3);
	p->mark_as_writing(piece_block(4, 3), &peer_struct);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == &peer_struct);
	p->piece_info(4, st);
	TEST_CHECK(st.requested == 0);
	TEST_CHECK(st.writing == 1);
	TEST_CHECK(st.finished == 3);
	TEST_CHECK(p->unverified_blocks() == 2 + 3);
	p->mark_as_finished(piece_block(4, 3), &peer_struct);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == &peer_struct);
	p->piece_info(4, st);
	TEST_CHECK(st.requested == 0);
	TEST_CHECK(st.writing == 0);
	TEST_CHECK(st.finished == 4);
	TEST_CHECK(p->unverified_blocks() == 2 + 4);
	p->we_have(4);
	p->piece_info(4, st);
	TEST_CHECK(st.requested == 0);
	TEST_CHECK(st.writing == 0);
	TEST_CHECK(st.finished == 4);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == 0);
	TEST_CHECK(p->unverified_blocks() == 2);

// ========================================================
	
	// test prefer_contiguous_blocks
	print_title("test prefer contiguous blocks");
	p = setup_picker("1111111", "       ", "", "");
	picked = pick_pieces(p, "*******", 1, 3 * blocks_per_piece
		, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 3 * blocks_per_piece);
	piece_block b = picked.front();
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index * blocks_per_piece + picked[i].block_index
			== b.piece_index * blocks_per_piece + b.block_index + 1);
		b = picked[i];
	}

	picked = pick_pieces(p, "*******", 1, 3 * blocks_per_piece
		, 0, options, empty_vector);
	TEST_CHECK(int(picked.size()) >= 3 * blocks_per_piece);
	b = picked.front();
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index * blocks_per_piece + picked[i].block_index
			== b.piece_index * blocks_per_piece + b.block_index + 1);
		b = picked[i];
	}

	// make sure pieces that don't match the 'whole pieces' requirement
	// are picked if there's no other choice
	p = setup_picker("1111111", "       ", "", "");
	p->mark_as_downloading(piece_block(2,2), &tmp1);
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece - 1, blocks_per_piece
		, 0, options, empty_vector);
	TEST_CHECK(picked.size() == 7 * blocks_per_piece - 1);
	TEST_CHECK(std::find(picked.begin(), picked.end(), piece_block(2,2)) == picked.end());

	// test aligned whole pieces
	print_title("test prefer aligned whole pieces");
	p = setup_picker("2222221222222222", "                ", "", "");
	picked = pick_pieces(p, "****************", 1, 4 * blocks_per_piece, 0
		, options | piece_picker::align_expanded_pieces, empty_vector);

	// the piece picker should pick piece 5, and then align it to even 4 pieces
	// i.e. it should have picked pieces: 4,5,6,7
	print_pick(picked);
	TEST_EQUAL(picked.size() , 4 * blocks_per_piece);

	std::set<int> picked_pieces;
	for (std::vector<piece_block>::iterator i = picked.begin()
		, end(picked.end()); i != end; ++i)
		picked_pieces.insert(picked_pieces.begin(), i->piece_index);

	TEST_CHECK(picked_pieces.size() == 4);
	int expected_pieces[] = {4,5,6,7};
	TEST_CHECK(std::equal(picked_pieces.begin(), picked_pieces.end(), expected_pieces))

//#error test picking with partial pieces and other peers present so that both backup_pieces and backup_pieces2 are used
	
// ========================================================

	// test parole mode
	print_title("test parole mode");
	p = setup_picker("3333133", "       ", "", "");
	p->mark_as_finished(piece_block(0, 0), 0);
	picked = pick_pieces(p, "*******", 1, blocks_per_piece, 0
		
		, options | piece_picker::on_parole | piece_picker::prioritize_partials, empty_vector);
	TEST_EQUAL(int(picked.size()), blocks_per_piece - 1);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(0, i + 1));

	//	make sure that the partial piece is not picked by a
	// peer that is has not downloaded/requested the other blocks
	picked = pick_pieces(p, "*******", 1, blocks_per_piece
		, &peer_struct
		, options | piece_picker::on_parole | piece_picker::prioritize_partials, empty_vector);
	TEST_EQUAL(int(picked.size()), blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(4, i));

// ========================================================

	// test suggested pieces
	print_title("test suggested pieces");
	p = setup_picker("1111222233334444", "                ", "", "");
	int v[] = {1, 5};
	const std::vector<int> suggested_pieces(v, v + 2);
	
	picked = pick_pieces(p, "****************", 1, blocks_per_piece
		, 0, options, suggested_pieces);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(1, i));
	p->set_piece_priority(0, 0);
	p->set_piece_priority(1, 0);
	p->set_piece_priority(2, 0);
	p->set_piece_priority(3, 0);

	picked = pick_pieces(p, "****************", 1, blocks_per_piece
		, 0, options, suggested_pieces);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(5, i));

	p = setup_picker("1111222233334444", "****            ", "", "");
	picked = pick_pieces(p, "****************", 1, blocks_per_piece
		, 0, options, suggested_pieces);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
		TEST_CHECK(picked[i] == piece_block(5, i));
	
// ========================================================

	// test bitfield optimization
	print_title("test bitfield optimization");
	// we have less than half of the pieces
	p = setup_picker("2122222211221222", "                ", "", "");
	// make sure it's not dirty
	pick_pieces(p, "****************", 1, blocks_per_piece, 0);
	print_availability(p);
	p->dec_refcount(string2vec("**  **  **  *   "), &tmp0);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1022112200220222"));
	// make sure it's not dirty
	pick_pieces(p, "****************", 1, blocks_per_piece, 0);
	p->inc_refcount(string2vec(" **  **  *   *  "), &tmp8);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1132123201220322"));

// ========================================================

	// test seed optimizaton
	print_title("test seed optimization");
	p = setup_picker("0000000000000000", "                ", "", "");

	// make sure it's not dirty
	pick_pieces(p, "****************", 1, blocks_per_piece, 0);

	p->inc_refcount_all(&tmp0);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1111111111111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, 0);
	p->dec_refcount(string2vec("  ****  **      "), &tmp0);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1100001100111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, 0);
	p->inc_refcount(string2vec("  ****  **      "), &tmp0);
	TEST_CHECK(verify_availability(p, "1111111111111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, 0);
	p->dec_refcount_all(&tmp0);
	TEST_CHECK(verify_availability(p, "0000000000000000"));

	p->inc_refcount_all(&tmp1);
	print_availability(p);
	TEST_CHECK(verify_availability(p, "1111111111111111"));

	pick_pieces(p, "****************", 1, blocks_per_piece, 0);
	p->dec_refcount(3, &tmp1);
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

// ========================================================

	// test reversed peers
	print_title("test reversed peers");
	p = setup_picker("3333333", "  *****", "", "");

	// a reversed peer picked a block from piece 0
	// This should make the piece reversed
	p->mark_as_downloading(piece_block(0,0), &tmp1
		, piece_picker::reverse);

	TEST_EQUAL(test_pick(p, piece_picker::rarest_first), 1);

	// make sure another reversed peer pick the same piece
	TEST_EQUAL(test_pick(p, piece_picker::rarest_first | piece_picker::reverse), 0);

// ========================================================

	// test reversed pieces upgrading to normal pieces
	print_title("test reversed piece upgrade");

	p = setup_picker("3333333", "  *****", "", "");

	// make piece 0 partial and reversed
	p->mark_as_downloading(piece_block(0,1), &tmp1
		, piece_picker::reverse);
	TEST_EQUAL(test_pick(p), 1);

	// now have a regular peer pick the reversed block. It should now
	// have turned into a regular one and be prioritized
	p->mark_as_downloading(piece_block(0,2), &tmp1);
	TEST_EQUAL(test_pick(p), 0);


// ========================================================

	// test pieces downgrading to reversed pieces
	print_title("test reversed piece downgrade");
// now make sure a piece can be demoted to reversed if there are no
// other outstanding requests

	p = setup_picker("3333333", "       ", "", "");

	// make piece 0 partial and not reversed
	p->mark_as_finished(piece_block(0,1), &tmp1);

	// a reversed peer picked a block from piece 0
	// This should make the piece reversed
	p->mark_as_downloading(piece_block(0,0), &tmp1
		, piece_picker::reverse);

	TEST_EQUAL(test_pick(p, piece_picker::rarest_first | piece_picker::reverse), 0);

// ========================================================

	print_title("test piece_stats");

	p = setup_picker("3456789", "*      ", "", "0300000");

	piece_picker::piece_stats_t stat = p->piece_stats(0);
	TEST_EQUAL(stat.peer_count, 3);
	TEST_EQUAL(stat.have, 1);
	TEST_EQUAL(stat.downloading, 0);

	stat = p->piece_stats(1);
	TEST_EQUAL(stat.peer_count, 4);
	TEST_EQUAL(stat.have, 0);
	TEST_EQUAL(stat.downloading, 1);

// ========================================================

	print_title("test piece passed");

	p = setup_picker("1111111", "*      ", "", "0300000");

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->num_have(), 1);

	p->piece_passed(1);
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->num_have(), 1);

	p->we_have(1);
	TEST_EQUAL(p->num_have(), 2);

	p->mark_as_finished(piece_block(2,0), &tmp1);
	p->piece_passed(2);
	TEST_EQUAL(p->num_passed(), 3);
	// just because the hash check passed doesn't mean
	// we "have" the piece. We need to write it to disk first
	TEST_EQUAL(p->num_have(), 2);

	// piece 2 already passed the hash check, as soon as we've
	// written all the blocks to disk, we should have that piece too
	p->mark_as_finished(piece_block(2,1), &tmp1);
	p->mark_as_finished(piece_block(2,2), &tmp1);
	p->mark_as_finished(piece_block(2,3), &tmp1);
	TEST_EQUAL(p->num_have(), 3);
	TEST_EQUAL(p->have_piece(2), true);

// ========================================================

	print_title("test piece passed (causing we_have)");

	p = setup_picker("1111111", "*      ", "", "0700000");

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->num_have(), 1);

	p->mark_as_finished(piece_block(1,3), &tmp1);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->num_have(), 1);

	p->piece_passed(1);
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->num_have(), 2);

// ========================================================

	print_title("test break_one_seed");

	p = setup_picker("0000000", "*      ", "", "0700000");
	p->inc_refcount_all(&tmp1);
	p->inc_refcount_all(&tmp2);
	p->inc_refcount_all(&tmp3);

	TEST_EQUAL(p->piece_stats(0).peer_count, 3);

	p->dec_refcount(0, &tmp1);

	TEST_EQUAL(p->piece_stats(0).peer_count, 2);
	TEST_EQUAL(p->piece_stats(1).peer_count, 3);
	TEST_EQUAL(p->piece_stats(2).peer_count, 3);
	TEST_EQUAL(p->piece_stats(3).peer_count, 3);

// ========================================================

	print_title("test we dont have");

	p = setup_picker("1111111", "* *    ", "1101111", "");
	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->has_piece_passed(2), true);
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->num_have(), 2);
	TEST_EQUAL(p->num_have_filtered(), 1);
	TEST_EQUAL(p->num_filtered(), 0);

	p->we_dont_have(0);

	TEST_EQUAL(p->has_piece_passed(0), false);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->has_piece_passed(2), true);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->num_have(), 1);
	TEST_EQUAL(p->num_have_filtered(), 1);

	p = setup_picker("1111111", "* *    ", "1101111", "");
	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->has_piece_passed(2), true);
	TEST_EQUAL(p->num_passed(), 2);
	TEST_EQUAL(p->num_have(), 2);
	TEST_EQUAL(p->num_have_filtered(), 1);
	TEST_EQUAL(p->num_filtered(), 0);

	p->we_dont_have(2);

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->has_piece_passed(2), false);
	TEST_EQUAL(p->num_passed(), 1);
	TEST_EQUAL(p->num_have(), 1);
	TEST_EQUAL(p->num_have_filtered(), 0);

// ========================================================

	print_title("test we dont have (don't have but passed hash check)");

	p = setup_picker("1111111", "* *    ", "1101111", "0200000");

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->have_piece(0), true)
	TEST_EQUAL(p->have_piece(1), false)

	p->piece_passed(1);

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), true);
	TEST_EQUAL(p->have_piece(1), false)

	p->we_dont_have(1);

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->have_piece(1), false)

// ========================================================

	print_title("test write_failed");

	p = setup_picker("1111111", "* *    ", "1101111", "0200000");

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->have_piece(1), false);

	p->piece_passed(1);

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), true);
	TEST_EQUAL(p->have_piece(1), false);

	p->mark_as_writing(piece_block(1, 0), &tmp1);
	p->write_failed(piece_block(1, 0));

	TEST_EQUAL(p->has_piece_passed(0), true);
	TEST_EQUAL(p->has_piece_passed(1), false);
	TEST_EQUAL(p->have_piece(1), false);

	// make sure write_failed() and lock_piece() actually
	// locks the piece, and that it won't be picked.
	// also make sure restore_piece() unlocks it and makes
	// it available for picking again.

	picked = pick_pieces(p, " *     ", 1, blocks_per_piece, 0);
	TEST_EQUAL(picked.size(), 0);

	p->restore_piece(1);

	picked = pick_pieces(p, " *     ", 1, blocks_per_piece, 0);
	TEST_EQUAL(picked.size(), blocks_per_piece);

	// locking pieces only works on partial pieces
	p->mark_as_writing(piece_block(1, 0), &tmp1);
	p->lock_piece(1);

	picked = pick_pieces(p, " *     ", 1, blocks_per_piece, 0);
	TEST_EQUAL(picked.size(), 0);

// ========================================================

	print_title("test write_failed (clear piece)");

	p = setup_picker("1111111", "* *    ", "1101111", "");

	stat = p->piece_stats(1);
	TEST_EQUAL(stat.downloading, 0);

	p->mark_as_writing(piece_block(1, 0), &tmp1);

	stat = p->piece_stats(1);
	TEST_EQUAL(stat.downloading, 1);

	p->write_failed(piece_block(1, 0));

	stat = p->piece_stats(1);
	TEST_EQUAL(stat.downloading, 0);

// ========================================================

	print_title("test mark_as_canceled");

	p = setup_picker("1111111", "* *    ", "1101111", "");

	stat = p->piece_stats(1);
	TEST_EQUAL(stat.downloading, 0);

	p->mark_as_writing(piece_block(1, 0), &tmp1);

	stat = p->piece_stats(1);
	TEST_EQUAL(stat.downloading, 1);

	p->mark_as_canceled(piece_block(1, 0), &tmp1);
	stat = p->piece_stats(1);
	TEST_EQUAL(stat.downloading, 0);

// ========================================================

	print_title("test get_download_queue");

	p = setup_picker("1111111", "       ", "1101111", "0327000");

	std::vector<piece_picker::downloading_piece> downloads
		= p->get_download_queue();

	// the download queue should have piece 1, 2 and 3 in it
	TEST_EQUAL(downloads.size(), 3);

	TEST_CHECK(std::find_if(downloads.begin(), downloads.end()
		, boost::bind(&piece_picker::downloading_piece::index, _1) == 1) != downloads.end());
	TEST_CHECK(std::find_if(downloads.begin(), downloads.end()
		, boost::bind(&piece_picker::downloading_piece::index, _1) == 2) != downloads.end());
	TEST_CHECK(std::find_if(downloads.begin(), downloads.end()
		, boost::bind(&piece_picker::downloading_piece::index, _1) == 3) != downloads.end());

// ========================================================

	print_title("test get_download_queue_size");

	p = setup_picker("1111111", "       ", "1111111", "0327ff0");

	TEST_EQUAL(p->get_download_queue_size(), 5);

	p->set_piece_priority(1, 0);

	int partial;
	int full;
	int finished;
	int zero_prio;
	p->get_download_queue_sizes(&partial, &full, &finished, &zero_prio);

	TEST_EQUAL(partial, 2);
	TEST_EQUAL(full, 0);
	TEST_EQUAL(finished, 2);
	TEST_EQUAL(zero_prio, 1);

// ========================================================

	print_title("test time_critical_mode");

	p = setup_picker("1111111", "       ", "1654741", "0352000");

	// rarest-first
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::rarest_first | piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	// reverse rarest-first
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::reverse | piece_picker::rarest_first
		| piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	// sequential
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::sequential | piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	// reverse sequential
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::reverse | piece_picker::sequential
		| piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	// just critical
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	// prioritize_partials
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::prioritize_partials | piece_picker::time_critical_mode, empty_vector);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	// even when a non-critical piece is suggested should we ignore it
	picked = pick_pieces(p, "*******", 7 * blocks_per_piece, 0, tmp_peer
		, piece_picker::rarest_first | piece_picker::time_critical_mode
		, suggested_pieces);
	TEST_EQUAL(picked.size(), blocks_per_piece);
	for (int i = 0; i < picked.size(); ++i)
		TEST_EQUAL(picked[0].piece_index, 4);

	return 0;
}

