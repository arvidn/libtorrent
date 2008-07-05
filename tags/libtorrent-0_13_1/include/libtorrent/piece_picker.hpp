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

		piece_picker(int blocks_per_piece
			, int total_num_blocks);

		void get_availability(std::vector<int>& avail) const;

		void set_sequenced_download_threshold(int sequenced_download_threshold);

		// the vector tells which pieces we already have
		// and which we don't have.
		void files_checked(
			std::vector<bool> const& pieces
			, std::vector<downloading_piece> const& unfinished
			, std::vector<int>& verify_pieces);

		// increases the peer count for the given piece
		// (is used when a HAVE or BITFIELD message is received)
		void inc_refcount(int index);

		// decreases the peer count for the given piece
		// (used when a peer disconnects)
		void dec_refcount(int index);
		
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
		void pick_pieces(std::vector<bool> const& pieces
			, std::vector<piece_block>& interesting_blocks
			, int num_pieces, int prefer_whole_pieces
			, void* peer, piece_state_t speed
			, bool rarest_first, bool on_parole
			, std::vector<int> const& suggested_pieces) const;

		// picks blocks from each of the pieces in the piece_list
		// vector that is also in the piece bitmask. The blocks
		// are added to interesting_blocks, and busy blocks are
		// added to backup_blocks. num blocks is the number of
		// blocks to be picked. Blocks are not picked from pieces
		// that are being downloaded
		int add_blocks(std::vector<int> const& piece_list
			, const std::vector<bool>& pieces
			, std::vector<piece_block>& interesting_blocks
			, int num_blocks, int prefer_whole_pieces
			, void* peer, std::vector<int> const& ignore) const;

		// picks blocks only from downloading pieces
		int add_blocks_downloading(
			std::vector<bool> const& pieces
			, std::vector<piece_block>& interesting_blocks
			, std::vector<piece_block>& backup_blocks
			, int num_blocks, int prefer_whole_pieces
			, void* peer, piece_state_t speed
			, bool on_parole) const;

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

#ifndef NDEBUG
		// used in debug mode
		void check_invariant(const torrent* t = 0) const;
		void verify_pick(std::vector<piece_block> const& picked
			, std::vector<bool> const& bitfield) const;
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

		bool can_pick(int piece, std::vector<bool> const& bitmask) const;
		std::pair<int, int> expand_piece(int piece, int whole_pieces
			, std::vector<bool> const& have) const;

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
			
			bool filtered() const { return piece_priority == filter_priority; }
			void filtered(bool f) { piece_priority = f ? filter_priority : 0; }
			
			int priority(int limit) const
			{
				if (downloading || filtered() || have()) return 0;
				// pieces we are currently downloading have high priority
				int prio = peer_count * 2;
				// if the peer_count is 0 or 1, the priority cannot be higher
				if (prio <= 1) return prio;
				if (prio >= limit * 2) prio = limit * 2;
				// the different priority levels
				switch (piece_priority)
				{
					case 1: return prio;
					case 2: return prio - 1;
					case 3: return (std::max)(prio / 2, 1);
					case 4: return (std::max)(prio / 2 - 1, 1);
					case 5: return (std::max)(prio / 3, 1);
					case 6: return (std::max)(prio / 3 - 1, 1);
					case 7: return 1;
				}
				return prio;
			}

			bool operator!=(piece_pos p) const
			{ return index != p.index || peer_count != p.peer_count; }

			bool operator==(piece_pos p) const
			{ return index == p.index && peer_count == p.peer_count; }

		};

		BOOST_STATIC_ASSERT(sizeof(piece_pos) == sizeof(char) * 4);

		bool is_ordered(int priority) const
		{
			return priority >= m_sequenced_download_threshold * 2;
		}

		void add(int index);
		void move(int vec_index, int elem_index);
		void sort_piece(std::vector<downloading_piece>::iterator dp);

		downloading_piece& add_download_piece();
		void erase_download_piece(std::vector<downloading_piece>::iterator i);

		// this vector contains all pieces we don't have.
		// in the first entry (index 0) is a vector of all pieces
		// that no peer have, the vector at index 1 contains
		// all pieces that exactly one peer have, index 2 contains
		// all pieces exactly two peers have and so on.
		// this is not entirely true. The availibility of a piece
		// is adjusted depending on its priority. But the principle
		// is that the higher index, the lower priority a piece has.
		std::vector<std::vector<int> > m_piece_info;

		// this maps indices to number of peers that has this piece and
		// index into the m_piece_info vectors.
		// piece_pos::we_have_index means that we have the piece, so it
		// doesn't exist in the piece_info buckets
		// pieces with the filtered flag set doesn't have entries in
		// the m_piece_info buckets either
		std::vector<piece_pos> m_piece_map;

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

		// the required popularity of a piece in order to download
		// it in sequence instead of random order.
		int m_sequenced_download_threshold;
#ifndef NDEBUG
		bool m_files_checked_called;
#endif
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

