/*

Copyright (c) 2003-2013, Arvid Norberg
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

#include "libtorrent/bitfield.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/peer_info.hpp" // for peer_info flags
#include "libtorrent/performance_counters.hpp" // for counters

#include <vector>

namespace libtorrent
{
	// returns the rank of a peer's source. We have an affinity
	// to connecting to peers with higher rank. This is to avoid
	// problems when our peer list is diluted by stale peers from
	// the resume data for instance
	int source_rank(int source_bitmask)
	{
		int ret = 0;
		if (source_bitmask & peer_info::tracker) ret |= 1 << 5;
		if (source_bitmask & peer_info::lsd) ret |= 1 << 4;
		if (source_bitmask & peer_info::dht) ret |= 1 << 3;
		if (source_bitmask & peer_info::pex) ret |= 1 << 2;
		return ret;
	}

	// the case where ignore_peer is motivated is if two peers
	// have only one piece that we don't have, and it's the
	// same piece for both peers. Then they might get into an
	// infinite loop, fighting to request the same blocks.
	void request_a_block(torrent& t, peer_connection& c)
	{
		if (t.is_seed()) return;
		if (c.no_download()) return;
		if (t.upload_mode()) return;
		if (c.is_disconnecting()) return;

		// don't request pieces before we have the metadata
		if (!t.valid_metadata()) return;

		// don't request pieces before the peer is properly
		// initialized after we have the metadata
		if (!t.are_files_checked()) return;

		TORRENT_ASSERT(c.peer_info_struct() != 0 || c.type() != peer_connection::bittorrent_connection);
		int num_requests = c.desired_queue_size()
			- (int)c.download_queue().size()
			- (int)c.request_queue().size();

#ifdef TORRENT_VERBOSE_LOGGING
		c.peer_log("*** PIECE_PICKER [ req: %d engame: %d ]", num_requests, c.endgame());
#endif
		TORRENT_ASSERT(c.desired_queue_size() > 0);
		// if our request queue is already full, we
		// don't have to make any new requests yet
		if (num_requests <= 0) return;

		t.need_picker();

		piece_picker& p = t.picker();
		std::vector<piece_block> interesting_pieces;
		interesting_pieces.reserve(100);

		int prefer_whole_pieces = c.prefer_whole_pieces();

		if (prefer_whole_pieces == 0)
		{
			prefer_whole_pieces = c.statistics().download_payload_rate()
				* t.settings().get_int(settings_pack::whole_pieces_threshold)
				> t.torrent_file().piece_length() ? 1 : 0;
		}
	
		// if we prefer whole pieces, the piece picker will pick at least
		// the number of blocks we want, but it will try to make the picked
		// blocks be from whole pieces, possibly by returning more blocks
		// than we requested.
#ifdef TORRENT_DEBUG
		error_code ec;
		TORRENT_ASSERT(c.remote() == c.get_socket()->remote_endpoint(ec) || ec);
#endif

		aux::session_interface& ses = t.session();

		ses.inc_stats_counter(counters::piece_picks);

		std::vector<pending_block> const& dq = c.download_queue();
		std::vector<pending_block> const& rq = c.request_queue();

		std::vector<int> const& suggested = c.suggested_pieces();
		bitfield const* bits = &c.get_bitfield();
		bitfield fast_mask;
		
		if (c.has_peer_choked())
		{
			// if we are choked we can only pick pieces from the
			// allowed fast set. The allowed fast set is sorted
			// in ascending priority order
			std::vector<int> const& allowed_fast = c.allowed_fast();

			// build a bitmask with only the allowed pieces in it
			fast_mask.resize(c.get_bitfield().size(), false);
			for (std::vector<int>::const_iterator i = allowed_fast.begin()
				, end(allowed_fast.end()); i != end; ++i)
				if ((*bits)[*i]) fast_mask.set_bit(*i);
			bits = &fast_mask;
		}

		piece_picker::piece_state_t state;
		peer_connection::peer_speed_t speed = c.peer_speed();
		if (speed == peer_connection::fast) state = piece_picker::fast;
		else if (speed == peer_connection::medium) state = piece_picker::medium;
		else state = piece_picker::slow;

		// picks the interesting pieces from this peer
		// the integer is the number of pieces that
		// should be guaranteed to be available for download
		// (if num_requests is too big, too many pieces are
		// picked and cpu-time is wasted)
		// the last argument is if we should prefer whole pieces
		// for this peer. If we're downloading one piece in 20 seconds
		// then use this mode.
		p.pick_pieces(*bits, interesting_pieces
			, num_requests, prefer_whole_pieces, c.peer_info_struct()
			, state, c.picker_options(), suggested, t.num_peers()
			, ses.stats_counters());

#ifdef TORRENT_VERBOSE_LOGGING
		c.peer_log("*** PIECE_PICKER [ prefer_whole: %d picked: %d ]"
			, prefer_whole_pieces, int(interesting_pieces.size()));
#endif

		// if the number of pieces we have + the number of pieces
		// we're requesting from is less than the number of pieces
		// in the torrent, there are still some unrequested pieces
		// and we're not strictly speaking in end-game mode yet
		// also, if we already have at least one outstanding
		// request, we shouldn't pick any busy pieces either
		bool dont_pick_busy_blocks = (ses.settings().get_bool(settings_pack::strict_end_game_mode)
			&& p.get_download_queue_size() < p.num_want_left())
			|| dq.size() + rq.size() > 0;

		// this is filled with an interesting piece
		// that some other peer is currently downloading
		piece_block busy_block = piece_block::invalid;

		for (std::vector<piece_block>::iterator i = interesting_pieces.begin();
			i != interesting_pieces.end(); ++i)
		{
			ses.inc_stats_counter(counters::piece_picker_blocks);

			if (prefer_whole_pieces == 0 && num_requests <= 0) break;

			int num_block_requests = p.num_peers(*i);
			if (num_block_requests > 0)
			{
				// have we picked enough pieces?
				if (num_requests <= 0) break;

				// this block is busy. This means all the following blocks
				// in the interesting_pieces list are busy as well, we might
				// as well just exit the loop
				if (dont_pick_busy_blocks) break;

				TORRENT_ASSERT(p.num_peers(*i) > 0);
				busy_block = *i;
				continue;
			}

			TORRENT_ASSERT(p.num_peers(*i) == 0);

			// don't request pieces we already have in our request queue
			// This happens when pieces time out or the peer sends us
			// pieces we didn't request. Those aren't marked in the
			// piece picker, but we still keep track of them in the
			// download queue
			if (std::find_if(dq.begin(), dq.end(), has_block(*i)) != dq.end()
				|| std::find_if(rq.begin(), rq.end(), has_block(*i)) != rq.end())
			{
#ifdef TORRENT_DEBUG
				std::vector<pending_block>::const_iterator j
					= std::find_if(dq.begin(), dq.end(), has_block(*i));
				if (j != dq.end()) TORRENT_ASSERT(j->timed_out || j->not_wanted);
#endif
#ifdef TORRENT_VERBOSE_LOGGING
				c.peer_log("*** PIECE_PICKER [ not_picking: %d,%d already in queue ]"
					, i->piece_index, i->block_index);
#endif
				continue;
			}

			// ok, we found a piece that's not being downloaded
			// by somebody else. request it from this peer
			// and return
			if (!c.add_request(*i, 0)) continue;
			TORRENT_ASSERT(p.num_peers(*i) == 1);
			TORRENT_ASSERT(p.is_requested(*i));
			num_requests--;
		}

		// we have picked as many blocks as we should
		// we're done!
		if (num_requests <= 0)
		{
			// since we could pick as many blocks as we
			// requested without having to resort to picking
			// busy ones, we're not in end-game mode
			c.set_endgame(false);
			return;
		}

		// we did not pick as many pieces as we wanted, because
		// there aren't enough. This means we're in end-game mode
		// as long as we have at least one request outstanding,
		// we shouldn't pick another piece
		// if we are attempting to download 'allowed' pieces
		// and can't find any, that doesn't count as end-game
		if (!c.has_peer_choked())
			c.set_endgame(true);
	
		// if we don't have any potential busy blocks to request
		// or if we already have outstanding requests, don't
		// pick a busy piece
		if (busy_block == piece_block::invalid
			|| dq.size() + rq.size() > 0)
		{
			return;
		}

		ses.inc_stats_counter(counters::end_game_piece_picker_blocks);

#ifdef TORRENT_DEBUG
		piece_picker::downloading_piece st;
		p.piece_info(busy_block.piece_index, st);
		TORRENT_ASSERT(st.requested + st.finished + st.writing
			== p.blocks_in_piece(busy_block.piece_index));
#endif
		TORRENT_ASSERT(p.is_requested(busy_block));
		TORRENT_ASSERT(!p.is_downloaded(busy_block));
		TORRENT_ASSERT(!p.is_finished(busy_block));
		TORRENT_ASSERT(p.num_peers(busy_block) > 0);

		c.add_request(busy_block, peer_connection::req_busy);
	}

}

