/*

Copyright (c) 2013, Arvid Norberg
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

#ifndef TORRENT_PERFORMANCE_COUNTERS_HPP_INCLUDED
#define TORRENT_PERFORMANCE_COUNTERS_HPP_INCLUDED

#include <boost/cstdint.hpp>

namespace libtorrent
{
	struct counters
	{
		enum stats_counter_t
		{
			// the number of peers that were disconnected this
			// tick due to protocol error
			error_peers,
			disconnected_peers,
			eof_peers,
			connreset_peers,
			connrefused_peers,
			connaborted_peers,
			perm_peers,
			buffer_peers,
			unreachable_peers,
			broken_pipe_peers,
			addrinuse_peers,
			no_access_peers,
			invalid_arg_peers,
			aborted_peers,

			piece_requests,
			max_piece_requests,
			invalid_piece_requests,
			choked_piece_requests,
			cancelled_piece_requests,
			piece_rejects,
			error_incoming_peers,
			error_outgoing_peers,
			error_rc4_peers,
			error_encrypted_peers,
			error_tcp_peers,
			error_utp_peers,
			// the number of times the piece picker fell through
			// to the end-game mode
			end_game_piece_picker_blocks,
			piece_picker_blocks,
			piece_picks,
			reject_piece_picks,
			unchoke_piece_picks,
			incoming_redundant_piece_picks,
			incoming_piece_picks,
			end_game_piece_picks,
			snubbed_piece_picks,

			// these counters indicate which parts
			// of the piece picker CPU is spent in
			piece_picker_partial_loops,
			piece_picker_suggest_loops,
			piece_picker_sequential_loops,
			piece_picker_reverse_rare_loops,
			piece_picker_rare_loops,
			piece_picker_rand_start_loops,
			piece_picker_rand_loops,
			piece_picker_busy_loops,

			// reasons to disconnect peers
			connect_timeouts,
			uninteresting_peers,
			timeout_peers,
			no_memory_peers,
			too_many_peers,
			transport_timeout_peers,
			num_banned_peers,
			banned_for_hash_failure,

			// connection attempts (not necessarily successful)
			connection_attempts,
			// successful incoming connections (not rejected for any reason)
			incoming_connections,

			// counts events where the network
			// thread wakes up
			on_read_counter,
			on_write_counter,
			on_tick_counter,
			on_lsd_counter,
			on_lsd_peer_counter,
			on_udp_counter,
			on_accept_counter,
			on_disk_queue_counter,
			on_disk_counter,

			torrent_evicted_counter,

			// bittorrent message counters
			// TODO: should keepalives be in here too?
			// how about dont-have, share-mode, upload-only
			num_incoming_choke,
			num_incoming_unchoke,
			num_incoming_interested,
			num_incoming_not_interested,
			num_incoming_have,
			num_incoming_bitfield,
			num_incoming_request,
			num_incoming_piece,
			num_incoming_cancel,
			num_incoming_dht_port,
			num_incoming_suggest,
			num_incoming_have_all,
			num_incoming_have_none,
			num_incoming_reject,
			num_incoming_allowed_fast,
			num_incoming_ext_handshake,
			num_incoming_pex,
			num_incoming_metadata,
			num_incoming_extended,

			num_outgoing_choke,
			num_outgoing_unchoke,
			num_outgoing_interested,
			num_outgoing_not_interested,
			num_outgoing_have,
			num_outgoing_bitfield,
			num_outgoing_request,
			num_outgoing_piece,
			num_outgoing_cancel,
			num_outgoing_dht_port,
			num_outgoing_suggest,
			num_outgoing_have_all,
			num_outgoing_have_none,
			num_outgoing_reject,
			num_outgoing_allowed_fast,
			num_outgoing_ext_handshake,
			num_outgoing_pex,
			num_outgoing_metadata,
			num_outgoing_extended,

			num_piece_passed,
			num_piece_failed,

			// TODO: 3 the _removed counters seem a bit silly
			num_piece_passed_removed,
			num_have_pieces,
			num_have_pieces_removed,
			num_total_pieces_added,
			num_total_pieces_removed,

			num_stats_counters
		};

		enum stats_gauges_t
		{
			num_checking_torrents = num_stats_counters,
			num_stopped_torrents,
			num_upload_only_torrents, // i.e. finished
			num_downloading_torrents,
			num_seeding_torrents,
			num_queued_seeding_torrents,
			num_queued_download_torrents,
			num_error_torrents,

			// the number of torrents that don't have the
			// IP filter applied to them.
			non_filter_torrents,

			// these counter indices deliberatly
			// match the order of socket type IDs
			// defined in socket_type.hpp.
			num_tcp_peers,
			num_socks5_peers,
			num_http_proxy_peers,
			num_utp_peers,
			num_i2p_peers,
			num_ssl_peers,
			num_ssl_socks5_peers,
			num_ssl_http_proxy_peers,
			num_ssl_utp_peers,

			num_peers_half_open,
			num_peers_connected,
			num_peers_up_interested,
			num_peers_down_interested,
			num_peers_up_unchoked,
			num_peers_down_unchoked,
			num_peers_up_requests,
			num_peers_down_requests,
			num_peers_up_disk,
			num_peers_down_disk,
			num_peers_end_game,

			num_counters,
			num_gauge_counters = num_counters - num_stats_counters
		};

		counters();
		void inc_stats_counter(int c, int value = 1);
		boost::int64_t operator[](int i) const;

	private:

		// TODO: some space could be saved here by making gauges 32 bits
		boost::int64_t m_stats_counter[num_counters];

	};
}

#endif

