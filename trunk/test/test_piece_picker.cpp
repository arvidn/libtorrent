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
#include "libtorrent/policy.hpp"
#include "libtorrent/bitfield.hpp"
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <algorithm>
#include <vector>
#include <set>

#include "test.hpp"

using namespace libtorrent;

const int blocks_per_piece = 4;

bitfield string2vec(char const* have_str)
{
	const int num_pieces = strlen(have_str);
	bitfield have(num_pieces, false);
	for (int i = 0; i < num_pieces; ++i)
		if (have_str[i] != ' ')  have.set_bit(i);
	return have;
}

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
	assert(int(strlen(have_str)) == num_pieces);

	boost::shared_ptr<piece_picker> p(new piece_picker);
	p->init(blocks_per_piece, num_pieces * blocks_per_piece);

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
		if (blocks & 1)
		{
			++counter;
			p->mark_as_finished(piece_block(i, 0), 0);
		}
		if (blocks & 2)
		{
			++counter;
			p->mark_as_finished(piece_block(i, 1), 0);
		}
		if (blocks & 4)
		{
			++counter;
			p->mark_as_finished(piece_block(i, 2), 0);
		}
		if (blocks & 8)
		{
			++counter;
			p->mark_as_finished(piece_block(i, 3), 0);
		}

		piece_picker::downloading_piece st;
		p->piece_info(i, st);
		TEST_CHECK(st.writing == 0);
		TEST_CHECK(st.requested == 0);
		TEST_CHECK(st.index == i);

		TEST_CHECK(st.finished == counter);
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

	for (int i = 0; i < num_pieces; ++i)
	{
		const int avail = availability[i] - '0';
		assert(avail >= 0);
		
		for (int j = 0; j < avail; ++j) p->inc_refcount(i);
	}

	std::vector<int> availability_vec;
	p->get_availability(availability_vec);
	for (int i = 0; i < num_pieces; ++i)
	{
		const int avail = availability[i] - '0';
		assert(avail >= 0);
		TEST_CHECK(avail == availability_vec[i]);
	}

#ifndef NDEBUG
	p->check_invariant();
#endif
	return p;
}

bool verify_pick(boost::shared_ptr<piece_picker> p
	, std::vector<piece_block> const& picked)
{
#ifndef NDEBUG
	p->check_invariant();
#endif
	for (std::vector<piece_block>::const_iterator i = picked.begin()
		, end(picked.end()); i != end; ++i)
	{
		if (p->num_peers(*i) > 0) return false;
	}

	// make sure there are no duplicated
	std::set<piece_block> blocks;
	std::copy(picked.begin(), picked.end(), std::insert_iterator<std::set<piece_block> >(blocks, blocks.end()));
	return picked.size() == blocks.size();
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

int test_pick(boost::shared_ptr<piece_picker> const& p)
{
	std::vector<piece_block> picked;
	const std::vector<int> empty_vector;
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 1);
	return picked[0].piece_index;
}

int test_main()
{

	tcp::endpoint endp;
	policy::peer peer_struct(endp, policy::peer::connectable, 0);
	std::vector<piece_block> picked;
	boost::shared_ptr<piece_picker> p;
	const std::vector<int> empty_vector;

	// make sure the block that is picked is from piece 1, since it
	// it is the piece with the lowest availability
	print_title("test pick lowest availability");
	p = setup_picker("2223333", "* * *  ", "", "");
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == 1);
	
// ========================================================

	// make sure the block that is picked is from piece 5, since it
	// has the highest priority among the available pieces
	print_title("test pick highest priority");
	p = setup_picker("1111111", "* * *  ", "1111122", "");
	picked.clear();
	p->pick_pieces(string2vec("****** "), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == 5);

// ========================================================

	// make sure the 4 blocks are picked from the same piece if
	// whole pieces are preferred. The only whole piece is 1.
	print_title("test pick whole pieces");
	p = setup_picker("1111111", "       ", "1111111", "1023460");
	picked.clear();
	p->pick_pieces(string2vec("****** "), picked, 1, 1, &peer_struct, piece_picker::fast, true, true, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 0; i < blocks_per_piece && i < int(picked.size()); ++i)
		TEST_CHECK(picked[i].piece_index == 1);

// ========================================================

	// test the distributed copies function. It should include ourself
	// in the availability. i.e. piece 0 has availability 2.
	// there are 2 pieces with availability 2 and 5 with availability 3
	print_title("test distributed copies");
	p = setup_picker("1233333", "*      ", "", "");
	float dc = p->distributed_copies();
	TEST_CHECK(fabs(dc - (2.f + 5.f / 7.f)) < 0.01f);

// ========================================================
	
	// make sure filtered pieces are ignored
	print_title("test filtered pieces");
	p = setup_picker("1111111", "       ", "0010000", "");
	picked.clear();
	p->pick_pieces(string2vec("*** ** "), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == 2);

// ========================================================
	
	// make sure we_dont_have works
	print_title("test we_dont_have");
	p = setup_picker("1111111", "*******", "0100000", "");
	picked.clear();
	p->we_dont_have(1);
	p->we_dont_have(2);
	p->pick_pieces(string2vec("*** ** "), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == 1);

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

	p->init(blocks_per_piece, blocks_per_piece * 7);
	TEST_CHECK(p->piece_priority(0) == 0);
	TEST_CHECK(p->num_filtered() == 1);
	TEST_CHECK(p->num_have_filtered() == 0);
	TEST_CHECK(p->num_have() == 0);

// ========================================================
	
	// make sure requested blocks aren't picked
	print_title("test don't pick requested blocks");
	p = setup_picker("1234567", "       ", "", "");
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front().piece_index == 0);
	piece_block first = picked.front();
	p->mark_as_downloading(picked.front(), &peer_struct, piece_picker::fast);
	TEST_CHECK(p->num_peers(picked.front()) == 1);
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) > 0);
	TEST_CHECK(picked.front() != first);
	TEST_CHECK(picked.front().piece_index == 0);

// ========================================================
/*
	// test sequenced download
	p = setup_picker("7654321", "       ", "", "");
	picked.clear();
	p->set_sequenced_download_threshold(3);
	p->pick_pieces(string2vec("*****  "), picked, 5 * blocks_per_piece, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 5 * blocks_per_piece);
	for (int i = 0; i < 5 * blocks_per_piece && i < int(picked.size()); ++i)
		TEST_CHECK(picked[i].piece_index == i / blocks_per_piece);

	picked.clear();
	p->set_sequenced_download_threshold(4);
	p->pick_pieces(string2vec("****   "), picked, 5 * blocks_per_piece, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 4 * blocks_per_piece);
	for (int i = 0; i < 4 * blocks_per_piece && i < int(picked.size()); ++i)
		TEST_CHECK(picked[i].piece_index == i / blocks_per_piece);

	picked.clear();
	p->set_sequenced_download_threshold(2);
	p->pick_pieces(string2vec("****** "), picked, 6 * blocks_per_piece, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 6 * blocks_per_piece);
	for (int i = 0; i < 6 * blocks_per_piece && i < int(picked.size()); ++i)
		TEST_CHECK(picked[i].piece_index == i / blocks_per_piece);
	
	picked.clear();
	p->set_piece_priority(0, 0);
	p->pick_pieces(string2vec("****** "), picked, 6 * blocks_per_piece, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 5 * blocks_per_piece);
	for (int i = 0; i < 5 * blocks_per_piece && i < int(picked.size()); ++i)
		TEST_CHECK(picked[i].piece_index == i / blocks_per_piece + 1);

	picked.clear();
	p->set_piece_priority(0, 1);
	p->pick_pieces(string2vec("****** "), picked, 6 * blocks_per_piece, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 6 * blocks_per_piece);
	for (int i = 0; i < 6 * blocks_per_piece && i < int(picked.size()); ++i)
		TEST_CHECK(picked[i].piece_index == i / blocks_per_piece);
*/
// ========================================================

	// test piece priorities
	print_title("test piece priorities");
	p = setup_picker("5555555", "       ", "3214576", "");
	TEST_CHECK(p->num_filtered() == 0);
	TEST_CHECK(p->num_have_filtered() == 0);
	p->set_piece_priority(0, 0);
	TEST_CHECK(p->num_filtered() == 1);
	TEST_CHECK(p->num_have_filtered() == 0);
	p->mark_as_finished(piece_block(0,0), 0);
	p->we_have(0);
	TEST_CHECK(p->num_filtered() == 0);
	TEST_CHECK(p->num_have_filtered() == 1);
	
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 6 * blocks_per_piece, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 6 * blocks_per_piece);
	TEST_CHECK(picked[0 * blocks_per_piece].piece_index == 5);
	// priority 5 and 6 is currently the same
	TEST_CHECK(picked[1 * blocks_per_piece].piece_index == 6 || picked[1 * blocks_per_piece].piece_index == 4);
	TEST_CHECK(picked[2 * blocks_per_piece].piece_index == 6 || picked[2 * blocks_per_piece].piece_index == 4);
	TEST_CHECK(picked[3 * blocks_per_piece].piece_index == 3);
	TEST_CHECK(picked[4 * blocks_per_piece].piece_index == 1);
	TEST_CHECK(picked[5 * blocks_per_piece].piece_index == 2);

	std::vector<int> prios;
	p->piece_priorities(prios);
	TEST_CHECK(prios.size() == 7);
	int prio_comp[] = {0, 2, 1, 4, 5, 7, 6};
	TEST_CHECK(std::equal(prios.begin(), prios.end(), prio_comp));
	
// ========================================================

	// test restore_piece
	print_title("test restore piece");
	p = setup_picker("1234567", "       ", "", "");
	p->mark_as_finished(piece_block(0,0), 0);
	p->mark_as_finished(piece_block(0,1), 0);
	p->mark_as_finished(piece_block(0,2), 0);
	p->mark_as_finished(piece_block(0,3), 0);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 1);

	p->restore_piece(0);	
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 0);

	p->mark_as_finished(piece_block(0,0), 0);
	p->mark_as_finished(piece_block(0,1), 0);
	p->mark_as_finished(piece_block(0,2), 0);
	p->mark_as_finished(piece_block(0,3), 0);
	p->set_piece_priority(0, 0);

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 1);

	p->restore_piece(0);	
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 1);

	p->set_piece_priority(0, 1);
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 1);
	TEST_CHECK(picked.front().piece_index == 0);

// ========================================================

	// test non-rarest-first mode
	print_title("test not rarest first");
	p = setup_picker("1234567", "* * *  ", "1111122", "");
	picked.clear();
	p->pick_pieces(string2vec("****** "), picked, 5 * blocks_per_piece, false, 0, piece_picker::fast, false, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) == 3 * blocks_per_piece);

	for (int i = 0; i < 4 * blocks_per_piece && i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index != 0);
		TEST_CHECK(picked[i].piece_index != 2);
		TEST_CHECK(picked[i].piece_index != 4);
	}

// ========================================================
	
	// test have_all and have_none
	print_title("test have_all and have_none");
	p = setup_picker("0123333", "*      ", "", "");
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (1.f + 5.f / 7.f)) < 0.01f);
	p->inc_refcount_all();
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (2.f + 5.f / 7.f)) < 0.01f);
	p->dec_refcount_all();
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (1.f + 5.f / 7.f)) < 0.01f);
	p->inc_refcount(0);
	p->dec_refcount_all();
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (0.f + 6.f / 7.f)) < 0.01f);
	TEST_CHECK(test_pick(p) == 2);

// ========================================================
	
	// test have_all and have_none with sequential download
	print_title("test have_all and have_none with sequential download");
	p = setup_picker("0123333", "*      ", "", "");
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (1.f + 5.f / 7.f)) < 0.01f);
	p->inc_refcount_all();
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (2.f + 5.f / 7.f)) < 0.01f);
	p->sequential_download(true);
	p->dec_refcount_all();
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (1.f + 5.f / 7.f)) < 0.01f);
	p->inc_refcount(0);
	p->dec_refcount_all();
	dc = p->distributed_copies();
	std::cout << "distributed copies: " << dc << std::endl;
	TEST_CHECK(fabs(dc - (0.f + 6.f / 7.f)) < 0.01f);
	p->inc_refcount(1);
	TEST_CHECK(test_pick(p) == 1);

// ========================================================

	// test inc_ref and dec_ref
	print_title("test inc_ref dec_ref");
	p = setup_picker("1233333", "     * ", "", "");
	TEST_CHECK(test_pick(p) == 0);

	p->dec_refcount(0);
	TEST_CHECK(test_pick(p) == 1);

	p->dec_refcount(4);
	p->dec_refcount(4);
	TEST_CHECK(test_pick(p) == 4);

	// decrease refcount on something that's not in the piece list
	p->dec_refcount(5);
	p->inc_refcount(5);
	
	p->inc_refcount(0);
	p->dec_refcount(4);
	TEST_CHECK(test_pick(p) == 0);

// ========================================================
/*	
	// test have_all and have_none, with a sequenced download threshold
	p = setup_picker("1233333", "*      ", "", "");
	p->set_sequenced_download_threshold(3);
	p->inc_refcount_all();
	dc = p->distributed_copies();
	TEST_CHECK(fabs(dc - (3.f + 5.f / 7.f)) < 0.01f);
	p->dec_refcount_all();
	dc = p->distributed_copies();
	TEST_CHECK(fabs(dc - (2.f + 5.f / 7.f)) < 0.01f);
	p->dec_refcount(2);
	dc = p->distributed_copies();
	TEST_CHECK(fabs(dc - (2.f + 4.f / 7.f)) < 0.01f);
	p->mark_as_downloading(piece_block(1,0), &peer_struct, piece_picker::fast);
	p->mark_as_downloading(piece_block(1,1), &peer_struct, piece_picker::fast);
	p->we_have(1);
	dc = p->distributed_copies();
	TEST_CHECK(fabs(dc - (2.f + 5.f / 7.f)) < 0.01f);
	picked.clear();
	// make sure it won't pick the piece we just got
	p->pick_pieces(string2vec(" * ****"), picked, 100, false, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 4 * blocks_per_piece);
	TEST_CHECK(picked[0 * blocks_per_piece].piece_index == 3);
	TEST_CHECK(picked[1 * blocks_per_piece].piece_index == 4);
	TEST_CHECK(picked[2 * blocks_per_piece].piece_index == 5);
	TEST_CHECK(picked[3 * blocks_per_piece].piece_index == 6);
*/	
// ========================================================
	
	// test unverified_blocks, marking blocks and get_downloader
	print_title("test unverified blocks");
	p = setup_picker("1111111", "       ", "", "0300700");
	TEST_CHECK(p->unverified_blocks() == 2 + 3);
	TEST_CHECK(p->get_downloader(piece_block(4, 0)) == 0);
	TEST_CHECK(p->get_downloader(piece_block(4, 1)) == 0);
	TEST_CHECK(p->get_downloader(piece_block(4, 2)) == 0);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == 0);
	p->mark_as_downloading(piece_block(4, 3), &peer_struct, piece_picker::fast);
	TEST_CHECK(p->get_downloader(piece_block(4, 3)) == &peer_struct);
	piece_picker::downloading_piece st;
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
	
	// test prefer_whole_pieces
	print_title("test prefer whole pieces");
	p = setup_picker("1111111", "       ", "", "");
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, 3, 0, piece_picker::fast, true, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 3 * blocks_per_piece);
	piece_block b = picked.front();
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index * blocks_per_piece + picked[i].block_index
			== b.piece_index * blocks_per_piece + b.block_index + 1);
		b = picked[i];
	}

	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, 3, 0, piece_picker::fast, false, false, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= 3 * blocks_per_piece);
	b = picked.front();
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index * blocks_per_piece + picked[i].block_index
			== b.piece_index * blocks_per_piece + b.block_index + 1);
		b = picked[i];
	}
	
// ========================================================

	// test parole mode
	print_title("test parole mode");
	p = setup_picker("3333133", "       ", "", "");
	p->mark_as_finished(piece_block(0, 0), 0);
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, 1, 0, piece_picker::fast, true, true, empty_vector);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= blocks_per_piece - 1);
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index == 0);
		TEST_CHECK(picked[i].block_index == i + 1);
	}

	//	make sure that the partial piece is not picked by a
	// peer that is has not downloaded/requested the other blocks
	picked.clear();
	p->pick_pieces(string2vec("*******"), picked, 1, 1, &peer_struct, piece_picker::fast, true, true, empty_vector);
	print_pick(picked);
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index == 4);
		TEST_CHECK(picked[i].block_index == i);
	}

// ========================================================

	// test suggested pieces
	print_title("test suggested pieces");
	p = setup_picker("1111222233334444", "                ", "", "");
	int v[] = {1, 5};
	std::vector<int> suggested_pieces(v, v + 2);
	
	picked.clear();
	p->pick_pieces(string2vec("****************"), picked, 1, 1, 0, piece_picker::fast, true, true, suggested_pieces);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index == 1);
		TEST_CHECK(picked[i].block_index == i);
	}
	p->set_piece_priority(0, 0);
	p->set_piece_priority(1, 0);
	p->set_piece_priority(2, 0);
	p->set_piece_priority(3, 0);

	picked.clear();
	p->pick_pieces(string2vec("****************"), picked, 1, 1, 0, piece_picker::fast, true, true, suggested_pieces);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index == 5);
		TEST_CHECK(picked[i].block_index == i);
	}

	p = setup_picker("1111222233334444", "****            ", "", "");
	picked.clear();
	p->pick_pieces(string2vec("****************"), picked, 1, 1, 0, piece_picker::fast, true, true, suggested_pieces);
	print_pick(picked);
	TEST_CHECK(verify_pick(p, picked));
	TEST_CHECK(int(picked.size()) >= blocks_per_piece);
	for (int i = 1; i < int(picked.size()); ++i)
	{
		TEST_CHECK(picked[i].piece_index == 5);
		TEST_CHECK(picked[i].block_index == i);
	}
	
// MISSING TESTS:
// 2. inc_ref() from 0 to 1 while sequenced download threshold is 1
// 4. filtered_pieces
// 5. clear peer
// 6. pick_pieces with prefer whole pieces
// 7. is_requested
// 8. is_downloaded
// 9. get_downloaders
// 10. abort_download

/*

	p.pick_pieces(peer1, picked, 1, false, 0, piece_picker::fast, true);
	TEST_CHECK(int(picked.size()) == 1);
	TEST_CHECK(picked.front().piece_index == 2);

	// now pick a piece from peer2. The block is supposed to be
	// from piece 3, since it is the rarest piece that peer has.
	picked.clear();
	p.pick_pieces(peer2, picked, 1, false, 0, piece_picker::fast, true);
	TEST_CHECK(int(picked.size()) == 1);
	TEST_CHECK(picked.front().piece_index == 3);

	// same thing for peer3.

	picked.clear();
	p.pick_pieces(peer3, picked, 1, false, 0, piece_picker::fast, true);
	TEST_CHECK(int(picked.size()) == 1);
	TEST_CHECK(picked.front().piece_index == 5);

	// now, if all peers would have piece 1 (the piece we have partially)
	// it should be prioritized over picking a completely new piece.
	peer3[1] = true;
	p.inc_refcount(1);
	
	picked.clear();
	p.pick_pieces(peer3, picked, 1, false, 0, piece_picker::fast, true);
	TEST_CHECK(int(picked.size()) == 1);
	TEST_CHECK(picked.front().piece_index == 1);
	// and the block picked should not be 0 or 2
	// since we already have those blocks

	TEST_CHECK(picked.front().block_index != 0);
	TEST_CHECK(picked.front().block_index != 2);

	// now, if we mark piece 1 and block 0 in piece 2
	// as being downloaded and picks a block from peer1,
	// it should pick a block from piece 2. But since
	// block 0 is marked as requested from another peer,
	// the piece_picker will continue to pick blocks
	// until it can return at least 1 block (since we
	// tell it we want one block) that is not being
	// downloaded from anyone else. This is to make it
	// possible for us to determine if we want to request
	// the block from more than one peer.
	// Both piece 1 and 2 are partial pieces, but pice
	// 2 is the rarest, so that's why it is picked.

	// we have block 0 and 2 already, so we can't mark
	// them as begin downloaded. 
	p.mark_as_downloading(piece_block(1, 1), &peer_struct, piece_picker::fast);
	p.mark_as_downloading(piece_block(1, 3), &peer_struct, piece_picker::fast);
	p.mark_as_downloading(piece_block(2, 0), &peer_struct, piece_picker::fast);

	std::vector<piece_picker::downloading_piece> const& downloads = p.get_download_queue();
	TEST_CHECK(downloads.size() == 2);
	TEST_CHECK(downloads[0].index == 1);
	TEST_CHECK(downloads[0].info[0].state == piece_picker::block_info::state_finished);
	TEST_CHECK(downloads[0].info[1].state == piece_picker::block_info::state_requested);
	TEST_CHECK(downloads[0].info[2].state == piece_picker::block_info::state_finished);
	TEST_CHECK(downloads[0].info[3].state == piece_picker::block_info::state_requested);

	TEST_CHECK(downloads[1].index == 2);
	TEST_CHECK(downloads[1].info[0].state == piece_picker::block_info::state_requested);
	TEST_CHECK(downloads[1].info[1].state == piece_picker::block_info::state_none);
	TEST_CHECK(downloads[1].info[2].state == piece_picker::block_info::state_none);
	TEST_CHECK(downloads[1].info[3].state == piece_picker::block_info::state_none);

	TEST_CHECK(p.is_requested(piece_block(1, 1)));
	TEST_CHECK(p.is_requested(piece_block(1, 3)));
	TEST_CHECK(p.is_requested(piece_block(2, 0)));
	TEST_CHECK(!p.is_requested(piece_block(2, 1)));

	picked.clear();
	p.pick_pieces(peer1, picked, 1, false, 0, piece_picker::fast, true);
	TEST_CHECK(int(picked.size()) == 2);

	piece_block expected3[] = { piece_block(2, 0), piece_block(2, 1) };
	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected3));

	// now, if we assume we're downloading at such a speed that
	// we might prefer to download whole pieces at a time from
	// this peer. It should not pick piece 1 or 2 (since those are
	// partially selected)

	picked.clear();
	p.pick_pieces(peer1, picked, 1, true, 0, piece_picker::fast, true);

	// it will pick 4 blocks, since we said we
	// wanted whole pieces.
	TEST_CHECK(int(picked.size()) == 4);

	piece_block expected4[] =
	{
		piece_block(3, 0), piece_block(3, 1)
		, piece_block(3, 2), piece_block(3, 3)
	};

	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected4));

	// now, try the same thing, but pick as many pieces as possible
	// to make sure it can still fall back on partial pieces

	picked.clear();
	p.pick_pieces(peer1, picked, 100, true, 0, piece_picker::fast, true);

	TEST_CHECK(int(picked.size()) == 12);

	piece_block expected5[] =
	{
		piece_block(3, 0), piece_block(3, 1)
		, piece_block(3, 2), piece_block(3, 3)
		, piece_block(5, 0), piece_block(5, 1)
		, piece_block(5, 2), piece_block(5, 3)
		, piece_block(2, 0), piece_block(2, 1)
		, piece_block(2, 2), piece_block(2, 3)
	};

	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected5));

	// now, try the same thing, but pick as many pieces as possible
	// to make sure it can still fall back on partial pieces

	picked.clear();
	p.pick_pieces(peer1, picked, 100, true, &peer_struct, piece_picker::fast, true);

	TEST_CHECK(int(picked.size()) == 11);

	piece_block expected6[] =
	{
		piece_block(2, 1), piece_block(2, 2)
		, piece_block(2, 3)
		, piece_block(3, 0), piece_block(3, 1)
		, piece_block(3, 2), piece_block(3, 3)
		, piece_block(5, 0), piece_block(5, 1)
		, piece_block(5, 2), piece_block(5, 3)
	};

	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected6));

	// make sure the piece picker allows filtered pieces
	// to become available
	p.mark_as_finished(piece_block(4, 2), 0);
*/
	return 0;
}

