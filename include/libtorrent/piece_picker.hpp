/*

Copyright (c) 2003-2018, Arvid Norberg
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

// heavy weight reference counting invariant checks
//#define TORRENT_DEBUG_REFCOUNTS

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <algorithm>
#include <vector>
#include <bitset>
#include <utility>
#include <set>

#include <boost/static_assert.hpp>
#include <boost/cstdint.hpp>
#include <boost/tuple/tuple.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/peer_id.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{

	class torrent;
	class peer_connection;
	struct bitfield;
	struct counters;
	struct torrent_peer;

	struct TORRENT_EXTRA_EXPORT piece_block
	{
		static const piece_block invalid;

		piece_block() {}
		piece_block(int p_index, int b_index)
			: piece_index(p_index)
			, block_index(b_index)
		{
		}
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

	class TORRENT_EXTRA_EXPORT piece_picker
	{
	public:

		enum
		{
			// the number of priority levels
			priority_levels = 8,
			// priority factor
			prio_factor = 3
		};

		struct block_info
		{
			block_info(): peer(0), num_peers(0), state(state_none) {}
			// the peer this block was requested or
			// downloaded from.
			torrent_peer* peer;
			// the number of peers that has this block in their
			// download or request queues
			unsigned num_peers:14;
			// the state of this block
			enum { state_none, state_requested, state_writing, state_finished };
			unsigned state:2;
#if TORRENT_USE_ASSERTS
			// to allow verifying the invariant of blocks belonging to the right piece
			int piece_index;
			std::set<torrent_peer*> peers;
#endif
		};

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
			sequential = 16,
			// treat pieces with priority 6 and below as filtered
			// to trigger end-game mode until all prio 7 pieces are
			// completed
			time_critical_mode = 32,
			// only expands pieces (when prefer contiguous blocks is set)
			// within properly aligned ranges, not the largest possible
			// range of pieces.
			align_expanded_pieces = 64
		};

		struct downloading_piece
		{
			downloading_piece() : index((std::numeric_limits<boost::uint32_t>::max)())
				, info_idx((std::numeric_limits<boost::uint16_t>::max)())
				, finished(0)
				, passed_hash_check(0)
				, writing(0)
				, locked(0)
				, requested(0)
				, outstanding_hash_check(0) {}

			bool operator<(downloading_piece const& rhs) const { return index < rhs.index; }

			// the index of the piece
			boost::uint32_t index;

			// info about each block in this piece. this is an index into the
			// m_block_info array, when multiplied by m_blocks_per_piece.
			// The m_blocks_per_piece following entries contain information about
			// all blocks in this piece.
			boost::uint16_t info_idx;

			// the number of blocks in the finished state
			boost::uint16_t finished:15;

			// set to true when the hash check job
			// returns with a valid hash for this piece.
			// we might not 'have' the piece yet though,
			// since it might not have been written to
			// disk. This is not set of locked is
			// set.
			boost::uint16_t passed_hash_check:1;

			// the number of blocks in the writing state
			boost::uint16_t writing:15;

			// when this is set, blocks from this piece may
			// not be picked. This is used when the hash check
			// fails or writing to the disk fails, while waiting
			// to synchronize the disk thread and clear out any
			// remaining state. Once this synchronization is
			// done, restore_piece() is called to clear the
			// locked flag.
			boost::uint16_t locked:1;

			// the number of blocks in the requested state
			boost::uint16_t requested:15;

			// set to true while there is an outstanding
			// hash check for this piece
			boost::uint16_t outstanding_hash_check:1;
		};

		piece_picker();

		void get_availability(std::vector<int>& avail) const;
		int get_availability(int piece) const;

		// increases the peer count for the given piece
		// (is used when a HAVE message is received)
		void inc_refcount(int index, const torrent_peer* peer);
		void dec_refcount(int index, const torrent_peer* peer);

		// increases the peer count for the given piece
		// (is used when a BITFIELD message is received)
		void inc_refcount(bitfield const& bitmask, const torrent_peer* peer);
		// decreases the peer count for the given piece
		// (used when a peer disconnects)
		void dec_refcount(bitfield const& bitmask, const torrent_peer* peer);

		// these will increase and decrease the peer count
		// of all pieces. They are used when seeds join
		// or leave the swarm.
		void inc_refcount_all(const torrent_peer* peer);
		void dec_refcount_all(const torrent_peer* peer);

		// This indicates that we just received this piece
		// it means that the refcounter will indicate that
		// we are not interested in this piece anymore
		// (i.e. we don't have to maintain a refcount)
		void we_have(int index);
		void we_dont_have(int index);

		// the lowest piece index we do not have
		int cursor() const { return m_cursor; }

		// one past the last piece we do not have.
		int reverse_cursor() const { return m_reverse_cursor; }

		// sets all pieces to dont-have
		void init(int blocks_per_piece, int blocks_in_last_piece, int total_num_pieces);
		int num_pieces() const { return int(m_piece_map.size()); }

		bool have_piece(int index) const;

		bool is_downloading(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < int(m_piece_map.size()));

			piece_pos const& p = m_piece_map[index];
			return p.downloading();
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
		// The last argument is the torrent_peer pointer for the peer that
		// we'll download from.
		// prefer_contiguous_blocks indicates how many blocks we would like
		// to request contiguously. The blocks are not merged by the piece
		// picker, but may be coalesced later by the peer_connection.
		// this feature is used by web_peer_connection to request larger blocks
		// at a time to mitigate limited pipelining and lack of keep-alive
		// (i.e. higher overhead per request).
		boost::uint32_t pick_pieces(bitfield const& pieces
			, std::vector<piece_block>& interesting_blocks, int num_blocks
			, int prefer_contiguous_blocks, torrent_peer* peer
			, int options, std::vector<int> const& suggested_pieces
			, int num_peers
			, counters& pc
			) const;

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
			, int num_blocks, int prefer_contiguous_blocks
			, torrent_peer* peer, std::vector<int> const& ignore
			, int options) const;

		// picks blocks only from downloading pieces
		int add_blocks_downloading(downloading_piece const& dp
			, bitfield const& pieces
			, std::vector<piece_block>& interesting_blocks
			, std::vector<piece_block>& backup_blocks
			, std::vector<piece_block>& backup_blocks2
			, int num_blocks, int prefer_contiguous_blocks
			, torrent_peer* peer
			, int options) const;

		// clears the peer pointer in all downloading pieces with this
		// peer pointer
		void clear_peer(torrent_peer* peer);

#if TORRENT_USE_INVARIANT_CHECKS
		// this is an invariant check
		void check_peers();
#endif

//		int get_block_state(piece_block block) const;

		// returns true if any client is currently downloading this
		// piece-block, or if it's queued for downloading by some client
		// or if it already has been successfully downloaded
		bool is_requested(piece_block block) const;
		// returns true if the block has been downloaded
		bool is_downloaded(piece_block block) const;
		// returns true if the block has been downloaded and written to disk
		bool is_finished(piece_block block) const;

		// marks this piece-block as queued for downloading
		// options are flags from options_t.
		bool mark_as_downloading(piece_block block, torrent_peer* peer
			, int options = 0);

		// returns true if the block was marked as writing,
		// and false if the block is already finished or writing
		bool mark_as_writing(piece_block block, torrent_peer* peer);

		void mark_as_canceled(piece_block block, torrent_peer* peer);
		void mark_as_finished(piece_block block, torrent_peer* peer);
		void mark_as_pad(piece_block block);
	
		// prevent blocks from being picked from this piece.
		// to unlock the piece, call restore_piece() on it
		void lock_piece(int piece);

		void write_failed(piece_block block);
		int num_peers(piece_block block) const;

		void piece_passed(int index);

//		void mark_as_checking(int index);
//		void mark_as_done_checking(int index);

		// returns information about the given piece
		void piece_info(int index, piece_picker::downloading_piece& st) const;

		struct piece_stats_t
		{
			int peer_count;
			int priority;
			bool have;
			bool downloading;
		};

		piece_stats_t piece_stats(int index) const;

		// if a piece had a hash-failure, it must be restored and
		// made available for redownloading
		void restore_piece(int index);

		// clears the given piece's download flag
		// this means that this piece-block can be picked again
		void abort_download(piece_block block, torrent_peer* peer = 0);

		// returns true if all blocks in this piece are finished
		// or if we have the piece
		bool is_piece_finished(int index) const;

		// returns true if we have the piece or if the piece
		// has passed the hash check
		bool has_piece_passed(int index) const;

		// returns the number of blocks there is in the given piece
		int blocks_in_piece(int index) const;

		// the number of downloaded blocks that hasn't passed
		// the hash-check yet
		int unverified_blocks() const;

		// return the peer pointers to all peers that participated in
		// this piece
		void get_downloaders(std::vector<torrent_peer*>& d, int index) const;

		std::vector<piece_picker::downloading_piece> get_download_queue() const;
		int get_download_queue_size() const;

		void get_download_queue_sizes(int* partial
			, int* full, int* finished, int* zero_prio) const;

		torrent_peer* get_downloader(piece_block block) const;

		// the number of filtered pieces we don't have
		int num_filtered() const { return m_num_filtered; }

		// the number of filtered pieces we already have
		int num_have_filtered() const { return m_num_have_filtered; }

		// number of pieces whose hash has passed _and_ they have
		// been successfully flushed to disk. Including pieces we have
		// also filtered with priority 0 but have anyway.
		int num_have() const { return m_num_have; }

		// number of pieces whose hash has passed (but haven't necessarily
		// been flushed to disk yet)
		int num_passed() const { return m_num_passed; }

		// return true if we have all the pieces we wanted
		bool is_finished() const { return m_num_have - m_num_have_filtered == int(m_piece_map.size()) - m_num_filtered; }

		bool is_seeding() const { return m_num_have == int(m_piece_map.size()); }

		// the number of pieces we want and don't have
		int num_want_left() const { return num_pieces() - m_num_have - m_num_filtered + m_num_have_filtered; }

#if TORRENT_USE_INVARIANT_CHECKS
		void check_piece_state() const;
		// used in debug mode
		void verify_priority(int start, int end, int prio) const;
		void verify_pick(std::vector<piece_block> const& picked
			, bitfield const& bits) const;

		void check_peer_invariant(bitfield const& have, torrent_peer const* p) const;
		void check_invariant(const torrent* t = 0) const;
#endif

		// functor that compares indices on downloading_pieces
		struct has_index
		{
			has_index(int i): index(boost::uint32_t(i)) { TORRENT_ASSERT(i >= 0); }
			bool operator()(const downloading_piece& p) const
			{ return p.index == index; }
			boost::uint32_t index;
		};

		int blocks_in_last_piece() const
		{ return m_blocks_in_last_piece; }

		std::pair<int, int> distributed_copies() const;

		void set_num_pad_files(int n) { m_num_pad_files = n; }

		// return the array of block_info objects for a given downloading_piece.
		// this array has m_blocks_per_piece elements in it
		block_info* blocks_for_piece(downloading_piece const& dp);
		block_info const* blocks_for_piece(downloading_piece const& dp) const;

	private:

		friend struct piece_pos;

		boost::tuple<bool, bool, int, int> requested_from(
			piece_picker::downloading_piece const& p
			, int num_blocks_in_piece, torrent_peer* peer) const;

		bool can_pick(int piece, bitfield const& bitmask) const;
		bool is_piece_free(int piece, bitfield const& bitmask) const;
		std::pair<int, int> expand_piece(int piece, int whole_pieces
			, bitfield const& have, int options) const;

		// only defined when TORRENT_PICKER_LOG is defined, used for debugging
		// unit tests
		void print_pieces() const;

		struct piece_pos
		{
			piece_pos() {}
			piece_pos(int peer_count_, int index_)
				: peer_count(unsigned(peer_count_))
				, download_state(piece_pos::piece_open)
				, piece_priority(4)
				, index(unsigned(index_))
			{
				TORRENT_ASSERT(peer_count_ >= 0);
				TORRENT_ASSERT(index_ >= 0);
			}

			// download_state of this piece.
			enum state_t
			{
				// the piece is partially downloaded or requested
				piece_downloading,
				// partial pieces where all blocks in the piece have been requested
				piece_full,
				// partial pieces where all blocks in the piece have been received
				// and are either finished or writing
				piece_finished,
				// partial pieces whose priority is 0
				piece_zero_prio,

				// the states up to this point indicate the piece is being
				// downloaded (or at least has a partially downloaded piece
				// in one of the m_downloads buckets).
				num_download_categories,

				// the piece is open to be picked
				piece_open = num_download_categories,

				// this is not a new download category/download list bucket.
				// it still goes into the piece_downloading bucket. However,
				// it indicates that this piece only has outstanding requests
				// from reverse peers. This is to de-prioritize it somewhat
				piece_downloading_reverse,
				piece_full_reverse
			};

			// returns one of the valid download categories of state_t or
			// piece_open if this piece is not being downloaded
			int download_queue() const
			{
				if (download_state == piece_downloading_reverse)
					return piece_downloading;
				if (download_state == piece_full_reverse)
					return piece_full;
				return download_state;
			}

			bool reverse() const
			{
				return download_state == piece_downloading_reverse
					|| download_state == piece_full_reverse;
			}

			void unreverse()
			{
				switch (download_state)
				{
					case piece_downloading_reverse:
						download_state = piece_downloading;
						break;
					case piece_full_reverse:
						download_state = piece_full;
						break;
				}
			}

			void make_reverse()
			{
				switch (download_state)
				{
					case piece_downloading:
						download_state = piece_downloading_reverse;
						break;
					case piece_full:
						download_state = piece_full_reverse;
						break;
				}
			}

			// the number of peers that has this piece
			// (availability)
#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
			boost::uint32_t peer_count : 9;
#else
			boost::uint32_t peer_count : 16;
#endif

			// one of the enums from state_t. This indicates whether this piece
			// is currently being downloaded or not, and what state it's in if
			// it is. Specifically, as an optimization, pieces that have all blocks
			// requested from them are separated out into separate lists to make
			// lookups quicker. The main oddity is that whether a downloading piece
			// has only been requested from peers that are reverse, that's
			// recorded as piece_downloading_reverse, which really means the same
			// as piece_downloading, it just saves space to also indicate that it
			// has a bit lower priority. The reverse bit is only relevant if the
			// state is piece_downloading.
			boost::uint32_t download_state : 3;

			// TODO: 2 having 8 priority levels is probably excessive. It should
			// probably be changed to 3 levels + dont-download

			// is 0 if the piece is filtered (not to be downloaded)
			// 1 is low priority
			// 2 is low priority
			// 3 is mid priority
			// 4 is default priority
			// 5 is mid priority
			// 6 is high priority
			// 7 is high priority
			boost::uint32_t piece_priority : 3;

			// index in to the piece_info vector
#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
			boost::uint32_t index : 17;
#else
			boost::uint32_t index;
#endif

#ifdef TORRENT_DEBUG_REFCOUNTS
			// all the peers that have this piece
			std::set<const torrent_peer*> have_peers;
#endif

			enum
			{
				// index is set to this to indicate that we have the
				// piece. There is no entry for the piece in the
				// buckets if this is the case.
#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
				we_have_index = 0x3ffff,
#else
				we_have_index = 0xffffffff,
#endif
				// the priority value that means the piece is filtered
				filter_priority = 0,
				// the max number the peer count can hold
#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
				max_peer_count = 0x1ff
#else
				max_peer_count = 0xffff
#endif
			};

			bool have() const { return index == we_have_index; }
			void set_have() { index = we_have_index; TORRENT_ASSERT(have()); }
			void set_not_have() { index = 0; TORRENT_ASSERT(!have()); }
			bool downloading() const { return download_state != piece_open; }

			bool filtered() const { return piece_priority == filter_priority; }

			// this function returns the effective priority of the piece. It's
			// actually the sort order of this piece compared to other pieces. A
			// lower index means it will be picked before a piece with a higher
			// index.
			// The availability of the piece (the number of peers that have this
			// piece) is fundamentally controlling the priority. It's multiplied
			// by 3 to form 3 levels of priority for each availability.
			//
			//  downloading pieces (not reverse)
			//   |   open pieces (not downloading)
			//   |   |   downloading pieces (reverse peers)
			//   |   |   |
			// +---+---+---+
			// | 0 | 1 | 2 |
			// +---+---+---+
			// this '3' is called prio_factor
			//
			// the manually set priority takes precedence over the availability
			// by multiplying availability by priority.

			int priority(piece_picker const* picker) const
			{
				// filtered pieces (prio = 0), pieces we have or pieces with
				// availability = 0 should not be present in the piece list
				// returning -1 indicates that they shouldn't.
				if (filtered() || have() || peer_count + picker->m_seeds == 0
					|| download_state == piece_full
					|| download_state == piece_finished)
					return -1;

				TORRENT_ASSERT(piece_priority > 0);

				// this is to keep downloading pieces at higher priority than
				// pieces that are not being downloaded, and to make reverse
				// downloading pieces to be lower priority
				int adjustment = -2;
				if (reverse()) adjustment = -1;
				else if (download_state != piece_open) adjustment = -3;

				// the + 1 here is because peer_count count be 0, it m_seeds
				// is > 0. We don't actually care about seeds (except for the
				// first one) since the order of the pieces is unaffected.
				int availability = int(peer_count) + 1;
				TORRENT_ASSERT(availability > 0);
				TORRENT_ASSERT(int(priority_levels - piece_priority) > 0);

				return availability * int(priority_levels - piece_priority)
					* prio_factor + adjustment;
			}

			bool operator!=(piece_pos p) const
			{ return index != p.index || peer_count != p.peer_count; }

			bool operator==(piece_pos p) const
			{ return index == p.index && peer_count == p.peer_count; }
		};

#ifndef TORRENT_DEBUG_REFCOUNTS
#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
		BOOST_STATIC_ASSERT(sizeof(piece_pos) == sizeof(char) * 4);
#else
		BOOST_STATIC_ASSERT(sizeof(piece_pos) == sizeof(char) * 8);
#endif
#endif

		bool partial_compare_rarest_first(downloading_piece const* lhs
		, downloading_piece const* rhs) const;

		void break_one_seed();

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

		typedef std::vector<downloading_piece>::iterator dlpiece_iter;
		dlpiece_iter add_download_piece(int index);
		void erase_download_piece(dlpiece_iter i);

		std::vector<downloading_piece>::const_iterator find_dl_piece(int queue, int index) const;
		std::vector<downloading_piece>::iterator find_dl_piece(int queue, int index);

		// returns an iterator to the downloading piece, whichever
		// download list it may live in now
		std::vector<downloading_piece>::iterator update_piece_state(
			std::vector<downloading_piece>::iterator dp);

	private:

		// the following vectors are mutable because they sometimes may
		// be updated lazily, triggered by const functions

		// this maps indices to number of peers that has this piece and
		// index into the m_piece_info vectors.
		// piece_pos::we_have_index means that we have the piece, so it
		// doesn't exist in the piece_info buckets
		// pieces with the filtered flag set doesn't have entries in
		// the m_piece_info buckets either
		// TODO: should this be allocated lazily?
		mutable std::vector<piece_pos> m_piece_map;

		// this maps pieces to a range of blocks that are pad files and should not
		// be picked
		// TOOD: this could be a much more efficient data structure
		std::set<piece_block> m_pad_blocks;

		// the number of seeds. These are not added to
		// the availability counters of the pieces
		int m_seeds;

		// the number of pieces that have passed the hash check
		int m_num_passed;

		// this vector contains all piece indices that are pickable
		// sorted by priority. Pieces are in random random order
		// among pieces with the same priority
		mutable std::vector<int> m_pieces;

		// these are indices to the priority boundries inside
		// the m_pieces vector. priority 0 always start at
		// 0, priority 1 starts at m_priority_boundries[0] etc.
		mutable std::vector<int> m_priority_boundries;

		// each piece that's currently being downloaded has an entry in this list
		// with block allocations. i.e. it says which parts of the piece that is
		// being downloaded. This list is ordered by piece index to make lookups
		// efficient there are as many buckets as there are piece states. See
		// piece_pos::state_t. The only download state that does not have a
		// corresponding downloading_piece vector is piece_open and
		// piece_downloading_reverse (the latter uses the same as
		// piece_downloading).
		std::vector<downloading_piece> m_downloads[piece_pos::num_download_categories];

		// this holds the information of the blocks in partially downloaded
		// pieces. the downloading_piece::info index point into this vector for
		// its storage
		std::vector<block_info> m_block_info;

		// these are block ranges in m_block_info that are free. The numbers
		// in here, when multiplied by m_blocks_per_piece is the index to the
		// first block in the range that's free to use by a new downloading_piece.
		// this is a free-list.
		std::vector<boost::uint16_t> m_free_block_infos;

		boost::uint16_t m_blocks_per_piece;
		boost::uint16_t m_blocks_in_last_piece;

		// the number of filtered pieces that we don't already
		// have. total_number_of_pieces - number_of_pieces_we_have
		// - num_filtered is supposed to the number of pieces
		// we still want to download
		int m_num_filtered;

		// the number of pieces we have that also are filtered
		int m_num_have_filtered;

		// we have all pieces in the range [0, m_cursor)
		// m_cursor is the first piece we don't have
		int m_cursor;

		// we have all pieces in the range [m_reverse_cursor, end)
		// m_reverse_cursor is the first piece where we also have
		// all the subsequent pieces
		int m_reverse_cursor;

		// the number of pieces we have (i.e. passed + flushed).
		// This includes pieces that we have filtered but still have
		int m_num_have;

		// this is the number of partial download pieces
		// that may be caused by pad files. We raise the limit
		// of number of partial pieces by this amount, to not
		// prioritize pieces that intersect pad files for no
		// apparent reason
		int m_num_pad_files;

		// if this is set to true, it means update_pieces()
		// has to be called before accessing m_pieces.
		mutable bool m_dirty;
	public:

#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
		enum { max_pieces = piece_pos::we_have_index - 1 };
#else
		enum { max_pieces = INT_MAX - 1 };
#endif

	};
}

#endif // TORRENT_PIECE_PICKER_HPP_INCLUDED

