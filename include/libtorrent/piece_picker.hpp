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
#include <cassert>

#include <boost/optional.hpp>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/socket.hpp"

namespace libtorrent
{

	class torrent;
	class address;
	class peer_connection;

	struct piece_block
	{
		piece_block(int p_index, int b_index)
			: piece_index(p_index)
			, block_index(b_index)
		{}
		int piece_index;
		int block_index;
		bool operator==(const piece_block& b) const
		{ return piece_index == b.piece_index && block_index == b.block_index; }

		bool operator!=(const piece_block& b) const
		{ return piece_index != b.piece_index || block_index != b.block_index; }

	};

	class piece_picker
	{
	public:

		enum { max_blocks_per_piece = 128 };

		struct block_info
		{
			block_info(): num_downloads(0) {}
			// the peer this block was requested or
			// downloaded from
			address peer;
			// the number of times this block has been downloaded
			int num_downloads;
		};

		struct downloading_piece
		{
			int index;
			// each bit represents a block in the piece
			// set to one if the block has been requested
			std::bitset<max_blocks_per_piece> requested_blocks;
			// the bit is set to one if the block has been acquired
			std::bitset<max_blocks_per_piece> finished_blocks;
			// info about each block
			block_info info[max_blocks_per_piece];

			// TODO: store a hash and a peer_connection reference
			// for each block. Then if the hash test fails on the
			// piece, redownload one block from another peer
			// then the first time, and check the hash again.
			// also maintain a counter how many times a piece-hash
			// has been confirmed. Download blocks that hasn't
			// been confirmed (since they are most probably the
			// invalid blocks)
		};

		piece_picker(int blocks_per_piece,
			int total_num_blocks);

		// this is called before any other method is called
		// after the local files has been checked.
		// the vector tells which pieces we already have
		// and which we don't have.
		void files_checked(
			const std::vector<bool>& pieces
			, const std::vector<downloading_piece>& unfinished);

		// increases the peer count for the given piece
		// (is used when a HAVE or BITFIELD message is received)
		void inc_refcount(int index);

		// decreases the peer count for the given piece
		// (used when a peer disconnects)
		void dec_refcount(int index);

		// This indicates that we just received this piece
		// it means that the refcounter will indicate that
		// we are not interested in this piece anymore
		// (i.e. we don't have to maintain a refcount)
		void we_have(int index);

		// pieces should be the vector that represents the pieces a
		// client has. It returns a list of all pieces that this client
		// has and that are interesting to download. It returns them in
		// priority order. It doesn't care about the download flag.
		// The user of this function must lookup if any piece is
		// marked as being downloaded. If the user of this function
		// decides to download a piece, it must mark it as being downloaded
		// itself, by using the mark_as_downloading() member function.
		// THIS IS DONE BY THE peer_connection::request_piece() MEMBER FUNCTION!
		void pick_pieces(const std::vector<bool>& pieces,
			std::vector<piece_block>& interesting_blocks,
			int num_pieces) const;

		// returns true if any client is currently downloading this
		// piece-block, or if it's queued for downloading by some client
		// or if it already has been successfully downlloaded
		bool is_downloading(piece_block block) const;

		bool is_finished(piece_block block) const;

		// marks this piece-block as queued for downloading
		void mark_as_downloading(piece_block block, const address& peer);
		void mark_as_finished(piece_block block, const address& peer);

		// if a piece had a hash-failure, it must be restured and
		// made available for redownloading
		void restore_piece(int index);

		// clears the given piece's download flag
		// this means that this piece-block can be picked again
		void abort_download(piece_block block);

		bool is_piece_finished(int index) const;

		// returns the number of blocks there is in the goven piece
		int blocks_in_piece(int index) const;

		// the number of downloaded blocks that hasn't passed
		// the hash-check yet
		int unverified_blocks() const;

		void get_downloaders(std::vector<address>& d, int index) const;

		const std::vector<downloading_piece>& get_download_queue() const
		{ return m_downloads; }

		boost::optional<address> get_downloader(piece_block block) const;

#ifndef NDEBUG
		// used in debug mode
		void integrity_check(const torrent* t = 0) const;
#endif

		// functor that compares indices on downloading_pieces
		struct has_index
		{
			has_index(int i): index(i) {}
			bool operator()(const downloading_piece& p) const
			{ return p.index == index; }
			int index;
		};

	private:

		struct piece_pos
		{
			piece_pos() {}
			piece_pos(int peer_count_, int index_)
				: peer_count(peer_count_)
				, downloading(0)
				, index(index_)
			{}

			// selects which vector to look in
			unsigned peer_count : 7;
			// is 1 if the piece is marked as being downloaded
			unsigned downloading : 1;
			// index in to the piece_info vector
			unsigned index : 24;

			bool operator!=(piece_pos p)
			{ return index != p.index || peer_count != p.peer_count; }

			bool operator==(piece_pos p)
			{ return index == p.index && peer_count == p.peer_count; }

		};


		void move(bool downloading, int vec_index, int elem_index);
		void remove(bool downloading, int vec_index, int elem_index);

		int add_interesting_blocks(const std::vector<int>& piece_list,
				const std::vector<bool>& pieces,
				std::vector<piece_block>& interesting_pieces,
				int num_blocks) const;

		// this vector contains all pieces we don't have.
		// in the first entry (index 0) is a vector of all pieces
		// that no peer have, the vector at index 1 contains
		// all pieces that exactly one peer have, index 2 contains
		// all pieces exactly two peers have and so on.
		std::vector<std::vector<int> > m_piece_info;

		// this vector has the same structure as m_piece_info
		// but only contains pieces we are currently downloading
		// they have higher priority than pieces we aren't downloading
		// during piece picking
		std::vector<std::vector<int> > m_downloading_piece_info;

		// this maps indices to number of peers that has this piece and
		// index into the m_piece_info vectors.
		// 0xffffff means that we have the piece, so it doesn't
		// exist in the piece_info buckets
		std::vector<piece_pos> m_piece_map;

		// each piece that's currently being downloaded
		// has an entry in this list with block allocations.
		// i.e. it says wich parts of the piece that
		// is being downloaded
		std::vector<downloading_piece> m_downloads;

		int m_blocks_per_piece;
		int m_blocks_in_last_piece;

	};

	inline int piece_picker::blocks_in_piece(int index) const
	{
		if (index+1 == m_piece_map.size())
			return m_blocks_in_last_piece;
		else
			return m_blocks_per_piece;
	}

}

#endif // TORRENT_PIECE_PICKER_HPP_INCLUDED
