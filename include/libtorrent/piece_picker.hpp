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
	};

	class piece_picker
	{
	public:

		enum { max_blocks_per_piece = 128 };

		piece_picker(int blocks_per_piece,
			int total_num_blocks);

		// this is called before any other method is called
		// after the local files has been checked.
		// the vector tells which pieces we already have
		// and which we don't have.
		void files_checked(const std::vector<bool>& pieces);

		// increases the peer count for the given piece
		// (is used when a HAVE or BITFIELD message is received)
		// returns true if this piece was interesting
		bool inc_refcount(int index);

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
			std::vector<piece_block>& interesting_blocks) const;

		// returns true if any client is currently downloading this
		// piece-block, or if it's queued for downloading by some client
		// or if it already has been successfully downlloaded
		bool is_downloading(piece_block block) const;

		// marks this piece-block as queued for downloading
		void mark_as_downloading(piece_block block);
		void mark_as_finished(piece_block block);

		// if a piece had a hash-failure, it must be restured and
		// made available fro redownloading
		void restore_piece(int index);

		// clears the given piece's download flag
		// this means that this piece-block can be picked again
		void abort_download(piece_block block);

		bool is_piece_finished(int index) const;


#ifndef NDEBUG
		// used in debug mode
		void integrity_check(const torrent* t = 0) const;
#endif

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

		struct downloading_piece
		{
			int index;
			std::bitset<max_blocks_per_piece> requested_blocks;
			std::bitset<max_blocks_per_piece> finished_blocks;
		};

		struct has_index
		{
			has_index(int i): index(i) {}
			bool operator()(const downloading_piece& p) const
			{ return p.index == index; }
			int index;
		};

		void move(bool downloading, int vec_index, int elem_index);
		void remove(bool downloading, int vec_index, int elem_index);

		bool add_interesting_blocks(const std::vector<int>& piece_list,
				const std::vector<bool>& pieces,
				std::vector<piece_block>& interesting_pieces) const;

		// this vector contains all pieces we don't have.
		// in the first entry (index 0) is a vector of all pieces
		// that no peer have, the vector at index 1 contains
		// all pieces that exactly one peer have, index 2 contains
		// all pieces exactly two peers have and so on.

		std::vector<std::vector<int> > m_piece_info;
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

}

#endif // TORRENT_PIECE_PICKER_HPP_INCLUDED
