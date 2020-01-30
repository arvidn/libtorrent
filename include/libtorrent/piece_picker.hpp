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

#include <algorithm>
#include <vector>
#include <utility>
#include <cstdint>
#include <tuple>
#include <set>
#include <unordered_map>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/piece_block.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/alert_types.hpp" // for picker_flags_t
#include "libtorrent/download_priority.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/units.hpp"

namespace libtorrent {

	class torrent;
	class peer_connection;
	template <typename Index>
	struct typed_bitfield;
	struct counters;
	struct torrent_peer;

	using prio_index_t = aux::strong_typedef<int, struct prio_index_tag_t>;
	using picker_options_t = flags::bitfield_flag<std::uint16_t, struct picker_options_tag>;
	using download_queue_t = aux::strong_typedef<std::uint8_t, struct dl_queue_tag>;
	using piece_extent_t = aux::strong_typedef<int, struct piece_extent_tag>;

	struct piece_count
	{
		// the number of pieces included in the "set"
		int num_pieces;
		// the number of blocks, out of those pieces, that are pad
		// blocks (i.e. entirely part of pad files)
		int pad_blocks;
		// true if the last piece is part of the set
		bool last_piece;
	};

	class TORRENT_EXTRA_EXPORT piece_picker
	{
		// only defined when TORRENT_PICKER_LOG is defined, used for debugging
		// unit tests
		friend void print_pieces(piece_picker const& p);

	public:

		enum
		{
			// the number of priority levels
			priority_levels = 8,
			// priority factor
			prio_factor = 3,
			// max blocks per piece
			// there are counters in downloading_piece that only have 15 bits to
			// count blocks per piece, that's restricting this
			max_blocks_per_piece = (1 << 15) - 1
		};

		struct block_info
		{
			block_info(): num_peers(0), state(state_none) {}
			// the peer this block was requested or
			// downloaded from.
			torrent_peer* peer = nullptr;
			// the number of peers that has this block in their
			// download or request queues
			unsigned num_peers:14;
			// the state of this block
			enum { state_none, state_requested, state_writing, state_finished };
			unsigned state:2;
#if TORRENT_USE_ASSERTS
			// to allow verifying the invariant of blocks belonging to the right piece
			piece_index_t piece_index{-1};
			std::set<torrent_peer*> peers;
#endif
		};

		// pick rarest first
		static constexpr picker_options_t rarest_first = 0_bit;

		// pick the most common first, or the last pieces if sequential
		static constexpr picker_options_t reverse = 1_bit;

		// only pick pieces exclusively requested from this peer
		static constexpr picker_options_t on_parole = 2_bit;

		// always pick partial pieces before any other piece
		static constexpr picker_options_t prioritize_partials = 3_bit;

		// pick pieces in sequential order
		static constexpr picker_options_t sequential = 4_bit;

		// treat pieces with priority 6 and below as filtered
		// to trigger end-game mode until all prio 7 pieces are
		// completed
		static constexpr picker_options_t time_critical_mode = 5_bit;

		// only expands pieces (when prefer contiguous blocks is set)
		// within properly aligned ranges, not the largest possible
		// range of pieces.
		static constexpr picker_options_t align_expanded_pieces = 6_bit;

		// this will create an affinity to pick pieces in extents of 4 MiB, in an
		// attempt to improve disk I/O by picking ranges of pieces (if pieces are
		// small)
		static constexpr picker_options_t piece_extent_affinity = 7_bit;

		struct downloading_piece
		{
			downloading_piece()
				: finished(0)
				, passed_hash_check(0)
				, writing(0)
				, locked(0)
				, requested(0)
				, outstanding_hash_check(0) {}

			bool operator<(downloading_piece const& rhs) const { return index < rhs.index; }

			// the index of the piece
			piece_index_t index{(std::numeric_limits<std::int32_t>::max)()};

			// info about each block in this piece. this is an index into the
			// m_block_info array, when multiplied by m_blocks_per_piece.
			// The m_blocks_per_piece following entries contain information about
			// all blocks in this piece.
			std::uint16_t info_idx{(std::numeric_limits<std::uint16_t>::max)()};

			// the number of blocks in the finished state
			std::uint16_t finished:15;

			// set to true when the hash check job
			// returns with a valid hash for this piece.
			// we might not 'have' the piece yet though,
			// since it might not have been written to
			// disk. This is not set of locked is
			// set.
			std::uint16_t passed_hash_check:1;

			// the number of blocks in the writing state
			std::uint16_t writing:15;

			// when this is set, blocks from this piece may
			// not be picked. This is used when the hash check
			// fails or writing to the disk fails, while waiting
			// to synchronize the disk thread and clear out any
			// remaining state. Once this synchronization is
			// done, restore_piece() is called to clear the
			// locked flag.
			std::uint16_t locked:1;

			// the number of blocks in the requested state
			std::uint16_t requested:15;

			// set to true while there is an outstanding
			// hash check for this piece
			std::uint16_t outstanding_hash_check:1;
		};

		piece_picker(int blocks_per_piece, int blocks_in_last_piece, int total_num_pieces);

		void get_availability(aux::vector<int, piece_index_t>& avail) const;
		int get_availability(piece_index_t piece) const;

		// increases the peer count for the given piece
		// (is used when a HAVE message is received)
		void inc_refcount(piece_index_t index, const torrent_peer* peer);
		void dec_refcount(piece_index_t index, const torrent_peer* peer);

		// increases the peer count for the given piece
		// (is used when a BITFIELD message is received)
		void inc_refcount(typed_bitfield<piece_index_t> const& bitmask
			, const torrent_peer* peer);
		// decreases the peer count for the given piece
		// (used when a peer disconnects)
		void dec_refcount(typed_bitfield<piece_index_t> const& bitmask
			, const torrent_peer* peer);

		// these will increase and decrease the peer count
		// of all pieces. They are used when seeds join
		// or leave the swarm.
		void inc_refcount_all(const torrent_peer* peer);
		void dec_refcount_all(const torrent_peer* peer);

		// we have every piece. This is used when creating a piece picker for a
		// seed
		void we_have_all();

		// This indicates that we just received this piece
		// it means that the refcounter will indicate that
		// we are not interested in this piece anymore
		// (i.e. we don't have to maintain a refcount)
		void we_have(piece_index_t index);
		void we_dont_have(piece_index_t index);

		// the lowest piece index we do not have
		piece_index_t cursor() const { return m_cursor; }

		// one past the last piece we do not have.
		piece_index_t reverse_cursor() const { return m_reverse_cursor; }

		// sets all pieces to dont-have
		void resize(int blocks_per_piece, int blocks_in_last_piece, int total_num_pieces);
		int num_pieces() const { return int(m_piece_map.size()); }

		bool have_piece(piece_index_t index) const;

		bool is_downloading(piece_index_t index) const
		{
			TORRENT_ASSERT(index >= piece_index_t(0));
			TORRENT_ASSERT(index < m_piece_map.end_index());

			piece_pos const& p = m_piece_map[index];
			return p.downloading();
		}

		// sets the priority of a piece.
		// returns true if the priority was changed from 0 to non-0
		// or vice versa
		bool set_piece_priority(piece_index_t index, download_priority_t prio);

		// returns the priority for the piece at 'index'
		download_priority_t piece_priority(piece_index_t index) const;

		// returns the current piece priorities for all pieces
		void piece_priorities(std::vector<download_priority_t>& pieces) const;

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
		picker_flags_t pick_pieces(typed_bitfield<piece_index_t> const& pieces
			, std::vector<piece_block>& interesting_blocks, int num_blocks
			, int prefer_contiguous_blocks, torrent_peer* peer
			, picker_options_t options, std::vector<piece_index_t> const& suggested_pieces
			, int num_peers
			, counters& pc
			) const;

		// picks blocks from each of the pieces in the piece_list
		// vector that is also in the piece bitmask. The blocks
		// are added to interesting_blocks, and busy blocks are
		// added to backup_blocks. num blocks is the number of
		// blocks to be picked. Blocks are not picked from pieces
		// that are being downloaded
		int add_blocks(piece_index_t piece, typed_bitfield<piece_index_t> const& pieces
			, std::vector<piece_block>& interesting_blocks
			, std::vector<piece_block>& backup_blocks
			, std::vector<piece_block>& backup_blocks2
			, int num_blocks, int prefer_contiguous_blocks
			, torrent_peer* peer, std::vector<piece_index_t> const& ignore
			, picker_options_t options) const;

		// picks blocks only from downloading pieces
		int add_blocks_downloading(downloading_piece const& dp
			, typed_bitfield<piece_index_t> const& pieces
			, std::vector<piece_block>& interesting_blocks
			, std::vector<piece_block>& backup_blocks
			, std::vector<piece_block>& backup_blocks2
			, int num_blocks, int prefer_contiguous_blocks
			, torrent_peer* peer
			, picker_options_t options) const;

		// clears the peer pointer in all downloading pieces with this
		// peer pointer
		void clear_peer(torrent_peer* peer);

#if TORRENT_USE_INVARIANT_CHECKS
		// this is an invariant check
		void check_peers();
#endif

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
			, picker_options_t options = {});

		// returns true if the block was marked as writing,
		// and false if the block is already finished or writing
		bool mark_as_writing(piece_block block, torrent_peer* peer);

		void mark_as_canceled(piece_block block, torrent_peer* peer);
		void mark_as_finished(piece_block block, torrent_peer* peer);

		void mark_as_pad(piece_block block);

		// prevent blocks from being picked from this piece.
		// to unlock the piece, call restore_piece() on it
		void lock_piece(piece_index_t piece);

		void write_failed(piece_block block);
		int num_peers(piece_block block) const;

		void piece_passed(piece_index_t index);

		// returns information about the given piece
		void piece_info(piece_index_t index, piece_picker::downloading_piece& st) const;

		struct piece_stats_t
		{
			int peer_count;
			int priority;
			bool have;
			bool downloading;
		};

		piece_stats_t piece_stats(piece_index_t index) const;

		// if a piece had a hash-failure, it must be restored and
		// made available for redownloading
		void restore_piece(piece_index_t index);

		// clears the given piece's download flag
		// this means that this piece-block can be picked again
		void abort_download(piece_block block, torrent_peer* peer = nullptr);

		// returns true if all blocks in this piece are finished
		// or if we have the piece
		bool is_piece_finished(piece_index_t index) const;

		// returns true if we have the piece or if the piece
		// has passed the hash check
		bool has_piece_passed(piece_index_t index) const;

		// returns the number of blocks there is in the given piece
		int blocks_in_piece(piece_index_t index) const;

		// return the peer pointers to all peers that participated in
		// this piece
		void get_downloaders(std::vector<torrent_peer*>& d, piece_index_t index) const;

		std::vector<piece_picker::downloading_piece> get_download_queue() const;
		int get_download_queue_size() const;

		void get_download_queue_sizes(int* partial
			, int* full, int* finished, int* zero_prio) const;

		torrent_peer* get_downloader(piece_block block) const;


		// piece states
		//
		//       have: -----------
		//     pieces: # # # # # # # # # # #
		//   filtered:         -------
		//   pads blk: ^       ^         ^
		//
		//  want-have: * * * *
		//       want: * * * *         * * *
		// total-have: * * * * * *
		//
		// we only care about:
		// 1. pieces we have (less pad blocks we have)
		// 2. pieces we have AND want (less pad blocks we have and want)
		// 3. pieces we want (less pad blocks we want)

		// number of pieces not filtered, as well as the number of
		// blocks out of those pieces that are pad blocks.
		// ``last_piece`` is set if the last piece is one of the
		// pieces.
		piece_count want() const;

		// number of pieces we have out of the ones we have not filtered
		piece_count have_want() const;

		// number of pieces we have (regardless of whether they are filtered)
		piece_count have() const;

		piece_count all_pieces() const;

		int pad_blocks_in_piece(piece_index_t const index) const;

		// number of pieces whose hash has passed (but haven't necessarily
		// been flushed to disk yet)
		int num_passed() const { return m_num_passed; }

		// return true if all the pieces we want have passed the hash check (but
		// may not have been written to disk yet)
		bool is_finished() const
		{
			// this expression warrants some explanation:
			// if the number of pieces we *want* to download
			// is less than or (more likely) equal to the number of pieces that
			// have passed the hash check (discounting the pieces that have passed
			// the check but then had their priority set to 0). Then we're
			// finished. Note that any piece we *have* implies it's both passed the
			// hash check *and* been written to disk.
			// num_pieces() - m_num_filtered - m_num_have_filtered
			//   <= (num_passed() - m_num_have_filtered)
			// this can be simplified. Note how m_num_have_filtered appears on both
			// side of the equation.
			//
			return num_pieces() - m_num_filtered <= num_passed();
		}

		bool is_seeding() const { return m_num_have == num_pieces(); }

		// the number of pieces we want and don't have
		int num_want_left() const { return num_pieces() - m_num_have - m_num_filtered + m_num_have_filtered; }

#if TORRENT_USE_INVARIANT_CHECKS
		void check_piece_state() const;
		// used in debug mode
		void verify_priority(prio_index_t start, prio_index_t end, int prio) const;
		void verify_pick(std::vector<piece_block> const& picked
			, typed_bitfield<piece_index_t> const& bits) const;

		void check_peer_invariant(typed_bitfield<piece_index_t> const& have
			, torrent_peer const* p) const;
		void check_invariant(const torrent* t = nullptr) const;
#endif

		// functor that compares indices on downloading_pieces
		struct has_index
		{
			explicit has_index(piece_index_t const i) : index(i)
			{ TORRENT_ASSERT(i >= piece_index_t(0)); }
			bool operator()(downloading_piece const& p) const
			{ return p.index == index; }
			piece_index_t const index;
		};

		int blocks_in_last_piece() const
		{ return m_blocks_in_last_piece; }

		std::pair<int, int> distributed_copies() const;

		// return the array of block_info objects for a given downloading_piece.
		// this array has m_blocks_per_piece elements in it
		span<block_info const> blocks_for_piece(downloading_piece const& dp) const;

	private:

		piece_extent_t extent_for(piece_index_t) const;
		index_range<piece_index_t> extent_for(piece_extent_t) const;

		void record_downloading_piece(piece_index_t const p);

		int num_pad_blocks() const { return m_num_pad_blocks; }

		span<block_info> mutable_blocks_for_piece(downloading_piece const& dp);

		std::tuple<bool, bool, int, int> requested_from(
			piece_picker::downloading_piece const& p
			, int num_blocks_in_piece, torrent_peer* peer) const;

		bool can_pick(piece_index_t piece, typed_bitfield<piece_index_t> const& bitmask) const;
		bool is_piece_free(piece_index_t piece, typed_bitfield<piece_index_t> const& bitmask) const;
		std::pair<piece_index_t, piece_index_t>
		expand_piece(piece_index_t piece, int whole_pieces
			, typed_bitfield<piece_index_t> const& have
			, picker_options_t options) const;

		struct piece_pos
		{
			piece_pos() {}
			piece_pos(int const peer_count_, int const index_)
				: peer_count(static_cast<std::uint16_t>(peer_count_))
				, download_state(static_cast<uint8_t>(piece_pos::piece_open))
				, piece_priority(static_cast<std::uint8_t>(default_priority))
				, index(index_)
			{
				TORRENT_ASSERT(peer_count_ >= 0);
				TORRENT_ASSERT(peer_count_ < (std::numeric_limits<std::uint16_t>::max)());
				TORRENT_ASSERT(index_ >= 0);
			}

			// the piece is partially downloaded or requested
			static constexpr download_queue_t piece_downloading{0};

			// partial pieces where all blocks in the piece have been requested
			static constexpr download_queue_t piece_full{1};
			// partial pieces where all blocks in the piece have been received
			// and are either finished or writing
			static constexpr download_queue_t piece_finished{2};
			// partial pieces whose priority is 0
			static constexpr download_queue_t piece_zero_prio{3};

			// the states up to this point indicate the piece is being
			// downloaded (or at least has a partially downloaded piece
			// in one of the m_downloads buckets).
			static constexpr download_queue_t num_download_categories{4};

			// the piece is open to be picked
			static constexpr download_queue_t piece_open{4};

			// this is not a new download category/download list bucket.
			// it still goes into the piece_downloading bucket. However,
			// it indicates that this piece only has outstanding requests
			// from reverse peers. This is to de-prioritize it somewhat
			static constexpr download_queue_t piece_downloading_reverse{5};
			static constexpr download_queue_t piece_full_reverse{6};

			// returns one of the valid download categories of state_t or
			// piece_open if this piece is not being downloaded
			download_queue_t download_queue() const
			{
				if (state() == piece_downloading_reverse)
					return piece_downloading;
				if (state() == piece_full_reverse)
					return piece_full;
				return state();
			}

			bool reverse() const
			{
				return state() == piece_downloading_reverse
					|| state() == piece_full_reverse;
			}

			void unreverse()
			{
				if (state() == piece_downloading_reverse)
						state(piece_downloading);
				else if (state() == piece_full_reverse)
						state(piece_full);
			}

			void make_reverse()
			{
				if (state() == piece_downloading)
						state(piece_downloading_reverse);
				else if (state() == piece_full)
						state(piece_full_reverse);
			}

			// the number of peers that has this piece
			// (availability)
			std::uint32_t peer_count : 26;

			// one of the download_queue_t values. This indicates whether this piece
			// is currently being downloaded or not, and what state it's in if
			// it is. Specifically, as an optimization, pieces that have all blocks
			// requested from them are separated out into separate lists to make
			// lookups quicker. The main oddity is that whether a downloading piece
			// has only been requested from peers that are reverse, that's
			// recorded as piece_downloading_reverse, which really means the same
			// as piece_downloading, it just saves space to also indicate that it
			// has a bit lower priority. The reverse bit is only relevant if the
			// state is piece_downloading.
			std::uint32_t download_state : 3;

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
			std::uint32_t piece_priority : 3;

			// index in to the piece_info vector
			prio_index_t index;

#ifdef TORRENT_DEBUG_REFCOUNTS
			// all the peers that have this piece
			std::set<const torrent_peer*> have_peers;
#endif

			// index is set to this to indicate that we have the
			// piece. There is no entry for the piece in the
			// buckets if this is the case.
			static constexpr prio_index_t we_have_index{-1};

			// the priority value that means the piece is filtered
			static constexpr std::uint32_t filter_priority = 0;

			// the max number the peer count can hold
			static constexpr std::uint32_t max_peer_count = 0xffff;

			bool have() const { return index == we_have_index; }
			void set_have() { index = we_have_index; TORRENT_ASSERT(have()); }
			void set_not_have() { index = prio_index_t(0); TORRENT_ASSERT(!have()); }
			bool downloading() const { return state() != piece_open; }

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
					|| state() == piece_full
					|| state() == piece_finished)
					return -1;

				TORRENT_ASSERT(piece_priority > 0);

				// this is to keep downloading pieces at higher priority than
				// pieces that are not being downloaded, and to make reverse
				// downloading pieces to be lower priority
				int adjustment = -2;
				if (reverse()) adjustment = -1;
				else if (state() != piece_open) adjustment = -3;

				// the + 1 here is because peer_count count be 0, it m_seeds
				// is > 0. We don't actually care about seeds (except for the
				// first one) since the order of the pieces is unaffected.
				int availability = int(peer_count) + 1;
				TORRENT_ASSERT(availability > 0);
				TORRENT_ASSERT(int(priority_levels - piece_priority) > 0);

				return availability * int(priority_levels - piece_priority)
					* prio_factor + adjustment;
			}

			bool operator!=(piece_pos const& p) const
			{ return index != p.index || peer_count != p.peer_count; }

			bool operator==(piece_pos const& p) const
			{ return index == p.index && peer_count == p.peer_count; }

			download_queue_t state() const { return download_queue_t(download_state); }
			void state(download_queue_t q) { download_state = static_cast<std::uint8_t>(q); }
		};

#ifndef TORRENT_DEBUG_REFCOUNTS
		static_assert(sizeof(piece_pos) == sizeof(char) * 8, "unexpected struct size");
#endif

		bool partial_compare_rarest_first(downloading_piece const* lhs
			, downloading_piece const* rhs) const;

		void break_one_seed();

		void update_pieces() const;

		prio_index_t priority_begin(int prio) const;
		prio_index_t priority_end(int prio) const;

		// fills in the range [start, end) of pieces in
		// m_pieces that have priority 'prio'
		std::pair<prio_index_t, prio_index_t> priority_range(int prio) const;

		// adds the piece 'index' to m_pieces
		void add(piece_index_t index);
		// removes the piece with the given priority and the
		// elem_index in the m_pieces vector
		void remove(int priority, prio_index_t elem_index);
		// updates the position of the piece with the given
		// priority and the elem_index in the m_pieces vector
		void update(int priority, prio_index_t elem_index);
		// shuffles the given piece inside it's priority range
		void shuffle(int priority, prio_index_t elem_index);

		std::vector<downloading_piece>::iterator add_download_piece(piece_index_t index);
		void erase_download_piece(std::vector<downloading_piece>::iterator i);

		std::vector<downloading_piece>::const_iterator find_dl_piece(download_queue_t, piece_index_t) const;
		std::vector<downloading_piece>::iterator find_dl_piece(download_queue_t, piece_index_t);

		// returns an iterator to the downloading piece, whichever
		// download list it may live in now
		std::vector<downloading_piece>::iterator update_piece_state(
			std::vector<downloading_piece>::iterator dp);

	private:

#if TORRENT_USE_ASSERTS || TORRENT_USE_INVARIANT_CHECKS
		index_range<download_queue_t> categories() const
		{ return {{}, piece_picker::piece_pos::num_download_categories}; }
#endif

		// the following vectors are mutable because they sometimes may
		// be updated lazily, triggered by const functions

		// this maps indices to number of peers that has this piece and
		// index into the m_piece_info vectors.
		// piece_pos::we_have_index means that we have the piece, so it
		// doesn't exist in the piece_info buckets
		// pieces with the filtered flag set doesn't have entries in
		// the m_piece_info buckets either
		// TODO: should this be allocated lazily?
		mutable aux::vector<piece_pos, piece_index_t> m_piece_map;

		// this indicates whether a block has been marked as a pad
		// block or not. It's indexed by block index, i.e. piece_index
		// * blocks_per_piece + block. These blocks should not be
		// picked and are considered to be had
		// TODO: this could be a much more efficient data structure
		bitfield m_pad_blocks;

		// tracks the number of blocks in a specific piece that are pad blocks
		std::unordered_map<piece_index_t, int> m_pads_in_piece;

		// when the adjecent_piece affinity is enabled, this contains the most
		// recent "extents" of adjecent pieces that have been requested from
		// this is mutable because it's updated by functions to pick pieces, which
		// are const. That's an efficient place to update it, since it's being
		// traversed already.
		mutable std::vector<piece_extent_t> m_recent_extents;

		// the number of bits set in the m_pad_blocks bitfield, i.e.
		// the number of blocks marked as pads
		int m_num_pad_blocks = 0;

		// the number of pad blocks that we already have
		int m_have_pad_blocks = 0;

		// the number of pad blocks part of filtered pieces we don't have
		int m_filtered_pad_blocks = 0;

		// the number of pad blocks we have that are also filtered
		int m_have_filtered_pad_blocks = 0;

		// the number of seeds. These are not added to
		// the availability counters of the pieces
		int m_seeds = 0;

		// the number of pieces that have passed the hash check
		int m_num_passed = 0;

		// this vector contains all piece indices that are pickable
		// sorted by priority. Pieces are in random random order
		// among pieces with the same priority
		mutable aux::vector<piece_index_t, prio_index_t> m_pieces;

		// these are indices to the priority boundaries inside
		// the m_pieces vector. priority 0 always start at
		// 0, priority 1 starts at m_priority_boundaries[0] etc.
		mutable aux::vector<prio_index_t> m_priority_boundaries;

		// each piece that's currently being downloaded has an entry in this list
		// with block allocations. i.e. it says which parts of the piece that is
		// being downloaded. This list is ordered by piece index to make lookups
		// efficient there are as many buckets as there are piece states. See
		// piece_pos::state_t. The only download state that does not have a
		// corresponding downloading_piece vector is piece_open and
		// piece_downloading_reverse (the latter uses the same as
		// piece_downloading).
		aux::array<aux::vector<downloading_piece>
			, static_cast<std::uint8_t>(piece_pos::num_download_categories)
			, download_queue_t> m_downloads;

		// this holds the information of the blocks in partially downloaded
		// pieces. the downloading_piece::info index point into this vector for
		// its storage
		aux::vector<block_info> m_block_info;

		// these are block ranges in m_block_info that are free. The numbers
		// in here, when multiplied by m_blocks_per_piece is the index to the
		// first block in the range that's free to use by a new downloading_piece.
		// this is a free-list.
		std::vector<std::uint16_t> m_free_block_infos;

		std::uint16_t m_blocks_per_piece = 0;
		std::uint16_t m_blocks_in_last_piece = 0;

		// the number of filtered pieces that we don't already
		// have. total_number_of_pieces - number_of_pieces_we_have
		// - num_filtered is supposed to the number of pieces
		// we still want to download
		// TODO: it would be more intuitive to account "wanted" pieces
		// instead of filtered
		int m_num_filtered = 0;

		// the number of pieces we have that also are filtered
		int m_num_have_filtered = 0;

		// we have all pieces in the range [0, m_cursor)
		// m_cursor is the first piece we don't have
		piece_index_t m_cursor{0};

		// we have all pieces in the range [m_reverse_cursor, end)
		// m_reverse_cursor is the first piece where we also have
		// all the subsequent pieces
		piece_index_t m_reverse_cursor{0};

		// the number of pieces we have (i.e. passed + flushed).
		// This includes pieces that we have filtered but still have
		int m_num_have = 0;

		// if this is set to true, it means update_pieces()
		// has to be called before accessing m_pieces.
		mutable bool m_dirty = false;
	public:

		enum { max_pieces = (std::numeric_limits<int>::max)() - 1 };

	};
}

#endif // TORRENT_PIECE_PICKER_HPP_INCLUDED
