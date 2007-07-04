#include "libtorrent/piece_picker.hpp"
#include "libtorrent/policy.hpp"

#include "test.hpp"

int test_main()
{
	using namespace libtorrent;

	tcp::endpoint endp;
	policy::peer peer_struct(endp, policy::peer::connectable, 0);

	const int num_pieces = 6;
		  
	// 4 blocks per piece
	const int blocks_per_piece = 4;
	piece_picker p(blocks_per_piece
		, num_pieces * blocks_per_piece);

	// we have the first piece
	std::vector<bool> have(num_pieces, false);
	have[0] = true;

	std::vector<piece_picker::downloading_piece> unfinished;
	piece_picker::downloading_piece partial;
	piece_picker::block_info blocks[blocks_per_piece * num_pieces];

	partial.index = 1;
	partial.info = blocks;
	partial.info[0].state = piece_picker::block_info::state_finished;
	partial.info[2].state = piece_picker::block_info::state_finished;
	unfinished.push_back(partial);
	
	std::vector<int> verify_pieces;
	p.files_checked(have, unfinished, verify_pieces);
	TEST_CHECK(p.is_finished(piece_block(1, 0)));
	TEST_CHECK(p.is_finished(piece_block(1, 2)));

	p.set_piece_priority(4, 0);

	TEST_CHECK(p.piece_priority(4) == 0);
	TEST_CHECK(p.piece_priority(3) == 1);
	
	p.set_piece_priority(3, 0);
	TEST_CHECK(p.piece_priority(3) == 0);
	p.set_piece_priority(3, 1);
	TEST_CHECK(p.piece_priority(3) == 1);

	TEST_CHECK(p.num_filtered() == 1);
	TEST_CHECK(p.num_have_filtered() == 0);

	std::vector<int> piece_priorities;
	p.piece_priorities(piece_priorities);
	int expected1[] = {1, 1, 1, 1, 0, 1};
	TEST_CHECK(std::equal(piece_priorities.begin()
		, piece_priorities.end(), expected1));

	std::vector<bool> peer1(num_pieces, false);
	std::vector<bool> peer2(num_pieces, false);
	std::vector<bool> peer3(num_pieces, false);

	peer1[2] = true;
	p.inc_refcount(2);

	peer1[3] = true;
	peer2[3] = true;
	p.inc_refcount(3);
	p.inc_refcount(3);

	peer1[4] = true;
	peer2[4] = true;
	peer3[4] = true;
	p.inc_refcount(4);
	p.inc_refcount(4);
	p.inc_refcount(4);

	peer1[5] = true;
	peer2[5] = true;
	peer3[5] = true;
	p.inc_refcount(5);
	p.inc_refcount(5);
	p.inc_refcount(5);
	
	// status for each piece:
	// 0: we have it
	// 1: we have block 0 and 2
	// 2: one peer has it
	// 3: two peers have it
	// 4: this piece is filtered so it should never be picked
	// 5: three peers have it

	// Now, we pick one block from peer1. The block is
	// is supposed to be picked from piece 2 since it is
	// the rarest piece.

	std::vector<piece_block> picked;
	picked.clear();
	p.pick_pieces(peer1, picked, 1, false, 0, piece_picker::fast);
	TEST_CHECK(picked.size() == 1);
	TEST_CHECK(picked.front().piece_index == 2);

	// now pick a piece from peer2. The block is supposed to be
	// from piece 3, since it is the rarest piece that peer has.
	picked.clear();
	p.pick_pieces(peer2, picked, 1, false, 0, piece_picker::fast);
	TEST_CHECK(picked.size() == 1);
	TEST_CHECK(picked.front().piece_index == 3);

	// same thing for peer3.

	picked.clear();
	p.pick_pieces(peer3, picked, 1, false, 0, piece_picker::fast);
	TEST_CHECK(picked.size() == 1);
	TEST_CHECK(picked.front().piece_index == 5);

	// now, if all peers would have piece 1 (the piece we have partially)
	// it should be prioritized over picking a completely new piece.
	peer3[1] = true;
	p.inc_refcount(1);
	
	picked.clear();
	p.pick_pieces(peer3, picked, 1, false, 0, piece_picker::fast);
	TEST_CHECK(picked.size() == 1);
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
	p.pick_pieces(peer1, picked, 1, false, 0, piece_picker::fast);
	TEST_CHECK(picked.size() == 2);

	piece_block expected3[] = { piece_block(2, 0), piece_block(2, 1) };
	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected3));

	// now, if we assume we're downloading at such a speed that
	// we might prefer to download whole pieces at a time from
	// this peer. It should not pick piece 1 or 2 (since those are
	// partially selected)

	picked.clear();
	p.pick_pieces(peer1, picked, 1, true, 0, piece_picker::fast);

	// it will pick 4 blocks, since we said we
	// wanted whole pieces.
	TEST_CHECK(picked.size() == 4);

	piece_block expected4[] =
	{
		piece_block(2, 0), piece_block(2, 1)
		, piece_block(2, 2), piece_block(2, 3)
	};
	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected4));

	// now, try the same thing, but pick as many pieces as possible
	// to make sure it can still fall back on partial pieces

	picked.clear();
	p.pick_pieces(peer1, picked, 100, true, 0, piece_picker::fast);

	TEST_CHECK(picked.size() == 12);

	piece_block expected5[] =
	{
		piece_block(2, 0), piece_block(2, 1)
		, piece_block(2, 2), piece_block(2, 3)
		, piece_block(3, 0), piece_block(3, 1)
		, piece_block(3, 2), piece_block(3, 3)
		, piece_block(5, 0), piece_block(5, 1)
		, piece_block(5, 2), piece_block(5, 3)
	};

	TEST_CHECK(std::equal(picked.begin()
		, picked.end(), expected5));

	// now, try the same thing, but pick as many pieces as possible
	// to make sure it can still fall back on partial pieces

	picked.clear();
	p.pick_pieces(peer1, picked, 100, true, &peer_struct, piece_picker::fast);

	TEST_CHECK(picked.size() == 11);

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
	
	return 0;
}

