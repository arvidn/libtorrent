/*

Copyright (c) 2003, Arvid Norberg
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
#ifndef TORRENT_PIECE_PICKER_HPP_INCLUDED
#define TORRENT_PIECE_PICKER_HPP_INCLUDED

#include <algorithm>
#include <vector>
#include <bitset>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/static_assert.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	class torrent;
	class peer_connection;
	struct bitfield;

	struct TORRENT_EXPORT piece_block
	{
		piece_block(int p_index, int b_index)
			: piece_index(p_index)
			, block_index(b_index)
		{}
		int piece_index;
		int block_index;

		bool operator<(piece_block const& b) const
		{
			if (piece_index < b.piece_index) return true;
			if (piece_index == b.piece_index) return block_index < b.block_index;
			return false;
		}

		bool operator==(piece_block const& b) const
		{ return piece_index == b.piece_index && block_index == b.block_index; }

		bool operator!=(piece_block const& b) const
		{ return piece_index != b.piece_index || block_index != b.block_index; }

	};

	class TORRENT_EXPORT piece_picker
	{
	public:

		enum
		{
			// the number of priority levels
			priority_levels = 8,
			// priority factor
			prio_factor = priority_levels - 4
		};

		struct block_info
		{
			block_info(): peer(0), num_peers(0), state(state_none) {}
			// the peer this block was requested or
			// downloaded from. This is a pointer to
			// a policy::peer object
			void* peer;
			// the number of peers that has this block in their
			// download or request queues
			unsigned num_peers:14;
			// the state of this block
			enum { state_none, state_requested, state_writing, state_finished };
			unsigned state:2;
		};

		// the peers that are downloading this piece
		// are considered fast peers or slow peers.
		// none is set if the blocks were downloaded
		// in a previous session
		enum piece_state_t
		{ none, slow, medium, fast };

		enum options_t
		{
			// pick rarest first 
			rarest_first = 1,
			// pick the most common first, or the last pieces if sequential
			reverse = 2,
			// only pick pieces exclusively requested from this peer
			on_parole = 4,
			// always pick partial pieces before any other piece
			prioritize_partials = 8,
			// pick pieces in sequential order
			sequential = 16
		};

		struct downloading_piece
		{
			downloading_piece(): finished(0), writing(0), requested(0) {}
			piece_state_t state;

			// the index of the piece
			int index;
			// info about each block
			// this is a pointer into the m_block_info
			// vector owned by the piece_picker
			block_info* info;
			// the number of blocks in the finished state
			boost::int16_t finished;
			// the number of blocks in the writing state
			boost::int16_t writing;
			// the number of blocks in the requested state
			boost::int16_t requested;
		};

		piece_picker();

		void get_availability(std::vector<int>& avail) const;

		// increases the peer count for the given piece
		// (is used when a HAVE message is received)
		void inc_refcount(int index);
		void dec_refcount(int index);

		// increases the peer count for the given piece
		// (is used when a BITFIELD message is received)
		void inc_refcount(bitfield const& bitmask);
		// decreases the peer count for the given piece
		// (used when a peer disconnects)
		void dec_refcount(bitfield const& bitmask);
		
		// these will increase and decrease the peer count
		// of all pieces. They are used when seeds join
		// or leave the swarm.
		void inc_refcount_all();
		void dec_refcount_all();

		// This indicates that we just received this piece
		// it means that the refcounter will indicate that
		// we are not interested in this piece anymore
		// (i.e. we don't have to maintain a refcount)
		void we_have(int index);
		void we_dont_have(int index);

		int cursor() const { return m_cursor; }
		int reverse_cursor() const { return m_reverse_cursor; }

		// sets all pieces to dont-have
		void init(int blocks_per_piece, int total_num_blocks);
		int num_pieces() const { return int(m_piece_map.size()); }

		bool have_piece(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < int(m_piece_map.size()));
			return m_piece_map[index].index == piece_pos::we_have_index;
		}

		// sets the priority of a piece.
		// returns true if the priority was changed from 0 to non-0
		// or vice versa
		bool set_piece_priority(int index, int prio);

		// returns the priority for the piece at 'index'
		int piece_priority(int index) const;

		// returns the current piece priorities for all pieces
		void piece_priorities(std::vector<int>& pieces) const;

		// ========== start deprecation ==============

		// fills the bitmask with 1's for pieces that are filtered
		void filtered_pieces(std::vector<bool>& mask) const;

		// ========== end deprecation ==============

		// pieces should be the vector that represents the pieces a
		// client has. It returns a list of all pieces that this client
		// has and that are interesting to download. It returns them in
		// priority order. It doesn't care about the download flag.
		// The user of this function must lookup if any piece is
		// marked as being downloaded. If the user of this function
		// decides to download a piece, it must mark it as being downloaded
		// itself, by using the mark_as_downloading() member function.
		// THIS IS DONE BY THE peer_connection::send_request() MEMBER FUNCTION!
		// The last argument is the policy::peer pointer for the peer that
		// we'll download from.
		void pick_pieces(bitfield const& pieces
			, std::vector<piece_block>& interesting_blocks, int num_blocks
			, int prefer_whole_pieces, void* peer, piece_state_t speed
			, int options, std::vector<int> const& suggested_pieces) const;

		// picks blocks from each of the pieces in the piece_list
		// vector that is also in the piece bitmask. The blocks
		// are added to interesting_blocks, and busy blocks are
		// added to backup_blocks. num blocks is the number of
		// blocks to be picked. Blocks are not picked from pieces
		// that are being downloaded
		int add_blocks(int piece, bitfield const& pieces
			, std::vector<piece_block>& interesting_blocks
			, std::vector<piece_block>& backup_blocks
			, std::vector<piece_block>& backup_blocks2
			, int num_blocks, int prefer_whole_pieces
			, void* peer, std::vector<int> const& ignore
			, piece_state_t speed, int options) const;

		// picks blocks only from downloading pieces
		int add_blocks_downloading(downloading_piece const& dp
			, bitfield const& pieces
			, std::vector<piece_block>& interesting_blocks
			, std::vector<piece_block>& backup_blocks
			, std::vector<piece_block>& backup_blocks2
			, int num_blocks, int prefer_whole_pieces
			, void* peer, piece_state_t speed
			, int options) const;

		// clears the peer pointer in all downloading pieces with this
		// peer pointer
		void clear_peer(void* peer);

		// returns true if any client is currently downloading this
		// piece-block, or if it's queued for downloading by some client
		// or if it already has been successfully downloaded
		bool is_requested(piece_block block) const;
		// returns true if the block has been downloaded
		bool is_downloaded(piece_block block) const;
		// returns true if the block has been downloaded and written to disk
		bool is_finished(piece_block block) const;

		// marks this piece-block as queued for downloading
		bool mark_as_downloading(piece_block block, void* peer
			, piece_state_t s);
		void mark_as_writing(piece_block block, void* peer);
		void mark_as_finished(piece_block block, void* peer);
		void write_failed(piece_block block);
		int num_peers(piece_block block) const;

		// returns information about the given piece
		void piece_info(int index, piece_picker::downloading_piece& st) const;
		
		// if a piece had a hash-failure, it must be restored and
		// made available for redownloading
		void restore_piece(int index);

		// clears the given piece's download flag
		// this means that this piece-block can be picked again
		void abort_download(piece_block block);

		bool is_piece_finished(int index) const;

		// returns the number of blocks there is in the given piece
		int blocks_in_piece(int index) const;

		// the number of downloaded blocks that hasn't passed
		// the hash-check yet
		int unverified_blocks() const;

		void get_downloaders(std::vector<void*>& d, int index) const;

		std::vector<downloading_piece> const& get_download_queue() const
		{ return m_downloads; }

		void* get_downloader(piece_block block) const;

		// the number of filtered pieces we don't have
		int num_filtered() const { return m_num_filtered; }

		// the number of filtered pieces we already have
		int num_have_filtered() const { return m_num_have_filtered; }

		int num_have() const { return m_num_have; }

#ifdef TORRENT_DEBUG
		// used in debug mode
		void verify_priority(int start, int end, int prio) const;
		void check_invariant(const torrent* t = 0) const;
		void verify_pick(std::vector<piece_block> const& picked
			, bitfield const& bits) const;
#endif
#if defined TORRENT_PICKER_LOG || defined TORRENT_DEBUG
		void print_pieces() const;
#endif

		// functor that compares indices on downloading_pieces
		struct has_index
		{
			has_index(int i): index(i) { TORRENT_ASSERT(i >= 0); }
			bool operator()(const downloading_piece& p) const
			{ return p.index == index; }
			int index;
		};

		int blocks_in_last_piece() const
		{ return m_blocks_in_last_piece; }

		float distributed_copies() const;

	private:

		friend struct piece_pos;

		bool can_pick(int piece, bitfield const& bitmask) const;
		bool is_piece_free(int piece, bitfield const& bitmask) const;
		std::pair<int, int> expand_piece(int piece, int whole_pieces
			, bitfield const& have) const;

		struct piece_pos
		{
			piece_pos() {}
			piece_pos(int peer_count_, int index_)
				: peer_count(peer_count_)
				, downloading(0)
				, piece_priority(1)
				, index(index_)
			{
				TORRENT_ASSERT(peer_count_ >= 0);
				TORRENT_ASSERT(index_ >= 0);
			}

			// the number of peers that has this piece
			// (availability)
			unsigned peer_count : 10;
			// is 1 if the piece is marked as being downloaded
			unsigned downloading : 1;
			// is 0 if the piece is filtered (not to be downloaded)
			// 1 is normal priority (default)
			// 2 is higher priority than pieces at the same availability level
			// 3 is same priority as partial pieces
			// 4 is higher priority than partial pieces
			// 5 and 6 same priority as availability 1 (ignores availability)
			// 7 is maximum priority (ignores availability)
			unsigned piece_priority : 3;
			// index in to the piece_info vector
			unsigned index : 18;

			enum
			{
				// index is set to this to indicate that we have the
				// piece. There is no entry for the piece in the
				// buckets if this is the case.
				we_have_index = 0x3ffff,
				// the priority value that means the piece is filtered
				filter_priority = 0,
				// the max number the peer count can hold
				max_peer_count = 0x3ff
			};
			
			bool have() const { return index == we_have_index; }
			void set_have() { index = we_have_index; TORRENT_ASSERT(have()); }
			void set_not_have() { index = 0; TORRENT_ASSERT(!have()); }
			
			bool filtered() const { return piece_priority == filter_priority; }
			void filtered(bool f) { piece_priority = f ? filter_priority : 0; }
			
			//  prio 7 is always top priority
			//  prio 0 is always -1 (don't pick)
			//  downloading pieces are always on an even prio_factor priority
			//
			//  availability x, downloading
			//   |   availability x, prio 3; availability 2x, prio 6
			//   |   |   availability x, prio 2; availability 2x, prio 5
			//   |   |   |   availability x, prio 1; availability 2x, prio 4
			//   |   |   |   |
			// +---+---+---+---+
			// | 0 | 1 | 2 | 3 |
			// +---+---+---+---+

			int priority(piece_picker const* picker) const
			{
				// filtered pieces (prio = 0), pieces we have or pieces with
				// availability = 0 should not be present in the piece list
				// returning -1 indicates that they shouldn't.
				if (filtered() || have() || peer_count + picker->m_seeds == 0)
					return -1;

				// prio 7 disregards availability
				if (piece_priority == priority_levels - 1) return 1 - downloading;

				// prio 4,5,6 halves the availability of a piece
				int availability = peer_count;
				int priority = piece_priority;
				if (piece_priority >= priority_levels / 2)
				{
					availability /= 2;
					priority -= (priority_levels - 2) / 2;
				}

				if (downloading) return availability * prio_factor;
				return availability * prio_factor + (priority_levels / 2) - priority;
			}

			bool operator!=(piece_pos p) const
			{ return index != p.index || peer_count != p.peer_count; }

			bool operator==(piece_pos p) const
			{ return index == p.index && peer_count == p.peer_count; }

		};

		BOOST_STATIC_ASSERT(sizeof(piece_pos) == sizeof(char) * 4);

		void update_pieces() const;

		// fills in the range [start, end) of pieces in
		// m_pieces that have priority 'prio'
		void priority_range(int prio, int* start, int* end);

		// adds the piece 'index' to m_pieces
		void add(int index);
		// removes the piece with the given priority and the
		// elem_index in the m_pieces vector
		void remove(int priority, int elem_index);
		// updates the position of the piece with the given
		// priority and the elem_index in the m_pieces vector
		void update(int priority, int elem_index);
		// shuffles the given piece inside it's priority range
		void shuffle(int priority, int elem_index);

		void sort_piece(std::vector<downloading_piece>::iterator dp);

		downloading_piece& add_download_piece();
		void erase_download_piece(std::vector<downloading_piece>::iterator i);

		// the number of seeds. These are not added to
		// the availability counters of the pieces
		int m_seeds;

		// the following vectors are mutable because they sometimes may
		// be updated lazily, triggered by const functions

		// this vector contains all piece indices that are pickable
		// sorted by priority. Pieces are in random random order
		// among pieces with the same priority
		mutable std::vector<int> m_pieces;

		// these are indices to the priority boundries inside
		// the m_pieces vector. priority 0 always start at
		// 0, priority 1 starts at m_priority_boundries[0] etc.
		mutable std::vector<int> m_priority_boundries;

		// this maps indices to number of peers that has this piece and
		// index into the m_piece_info vectors.
		// piece_pos::we_have_index means that we have the piece, so it
		// doesn't exist in the piece_info buckets
		// pieces with the filtered flag set doesn't have entries in
		// the m_piece_info buckets either
		mutable std::vector<piece_pos> m_piece_map;

		// each piece that's currently being downloaded
		// has an entry in this list with block allocations.
		// i.e. it says wich parts of the piece that
		// is being downloaded
		std::vector<downloading_piece> m_downloads;

		// this holds the information of the
		// blocks in partially downloaded pieces.
		// the first m_blocks_per_piece entries
		// in the vector belongs to the first
		// entry in m_downloads, the second
		// m_blocks_per_piece entries to the
		// second entry in m_downloads and so on.
		std::vector<block_info> m_block_info;

		int m_blocks_per_piece;
		int m_blocks_in_last_piece;

		// the number of filtered pieces that we don't already
		// have. total_number_of_pieces - number_of_pieces_we_have
		// - num_filtered is supposed to the number of pieces
		// we still want to download
		int m_num_filtered;

		// the number of pieces we have that also are filtered
		int m_num_have_filtered;
		
		// the number of pieces we have
		int m_num_have;

		// we have all pieces in the range [0, m_cursor)
		// m_cursor is the first piece we don't have
		int m_cursor;

		// we have all pieces in the range [m_reverse_cursor, end)
		// m_reverse_cursor is the first piece where we also have
		// all the subsequent pieces
		int m_reverse_cursor;

		// if this is set to true, it means update_pieces()
		// has to be called before accessing m_pieces.
		mutable bool m_dirty;
	public:

		enum { max_pieces = piece_pos::we_have_index - 1 };

	};

	inline int piece_picker::blocks_in_piece(int index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_map.size());
		if (index+1 == (int)m_piece_map.size())
			return m_blocks_in_last_piece;
		else
			return m_blocks_per_piece;
	}

}

#endif // TORRENT_PIECE_PICKER_HPP_INCLUDED

