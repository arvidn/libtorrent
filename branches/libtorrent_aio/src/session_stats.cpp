/*

Copyright (c) 2012, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include "libtorrent/session.hpp" // for stats_metric
#include "libtorrent/aux_/session_interface.hpp" // for stats counter names
#include "libtorrent/performance_counters.hpp" // for counters

namespace libtorrent
{
#define METRIC(category, name, type) { #category "." #name, counters:: name, stats_metric:: type},
	const static stats_metric metrics[] =
	{
		// ``error_peers`` is the total number of peer disconnects
		// caused by an error (not initiated by this client) and
		// disconnected initiated by this client (``disconnected_peers``).
		METRIC(peer, error_peers, type_counter)
		METRIC(peer, disconnected_peers, type_counter)

		// these counters break down the peer errors into more specific
		// categories. These errors are what the underlying transport
		// reported (i.e. TCP or uTP)
		METRIC(peer, eof_peers, type_counter)
		METRIC(peer, connreset_peers, type_counter)
		METRIC(peer, connrefused_peers, type_counter)
		METRIC(peer, connaborted_peers, type_counter)
		METRIC(peer, perm_peers, type_counter)
		METRIC(peer, buffer_peers, type_counter)
		METRIC(peer, unreachable_peers, type_counter)
		METRIC(peer, broken_pipe_peers, type_counter)
		METRIC(peer, addrinuse_peers, type_counter)
		METRIC(peer, no_access_peers, type_counter)
		METRIC(peer, invalid_arg_peers, type_counter)
		METRIC(peer, aborted_peers, type_counter)

		// these counters break down the peer errors into
		// whether they happen on incoming or outgoing peers.
		METRIC(peer, error_incoming_peers, type_counter)
		METRIC(peer, error_outgoing_peers, type_counter)

		// these counters break down the peer errors into
		// whether they happen on encrypted peers (just
		// encrypted handshake) and rc4 peers (full stream
		// encryption). These can indicate whether encrypted
		// peers are more or less likely to fail
		METRIC(peer, error_rc4_peers, type_counter)
		METRIC(peer, error_encrypted_peers, type_counter)

		// these counters break down the peer errors into
		// whether they happen on uTP peers or TCP peers.
		// these may indicate whether one protocol is
		// more error prone
		METRIC(peer, error_tcp_peers, type_counter)
		METRIC(peer, error_utp_peers, type_counter)

		// these counters break down the reasons to
		// disconnect peers.
		METRIC(peer, connect_timeouts, type_counter)
		METRIC(peer, uninteresting_peers, type_counter)
		METRIC(peer, timeout_peers, type_counter)
		METRIC(peer, no_memory_peers, type_counter)
		METRIC(peer, too_many_peers, type_counter)
		METRIC(peer, transport_timeout_peers, type_counter)
		METRIC(peer, num_banned_peers, type_counter)
		METRIC(peer, banned_for_hash_failure, type_counter)

		METRIC(peer, connection_attempts, type_counter)
		METRIC(peer, incoming_connections, type_counter)

		// the number of peer connections for each kind of socket.
		// these counts include half-open (connecting) peers.
		METRIC(peer, num_tcp_peers, type_gauge)
		METRIC(peer, num_socks5_peers, type_gauge)
		METRIC(peer, num_http_proxy_peers, type_gauge)
		METRIC(peer, num_utp_peers, type_gauge)
		METRIC(peer, num_i2p_peers, type_gauge)
		METRIC(peer, num_ssl_peers, type_gauge)
		METRIC(peer, num_ssl_socks5_peers, type_gauge)
		METRIC(peer, num_ssl_http_proxy_peers, type_gauge)
		METRIC(peer, num_ssl_utp_peers, type_gauge)

		METRIC(peer, num_peers_half_open, type_gauge)
		METRIC(peer, num_peers_connected, type_gauge)
		METRIC(peer, num_peers_up_interested, type_gauge)
		METRIC(peer, num_peers_down_interested, type_gauge)
		METRIC(peer, num_peers_up_unchoked, type_gauge)
		METRIC(peer, num_peers_down_unchoked, type_gauge)
		METRIC(peer, num_peers_up_requests, type_gauge)
		METRIC(peer, num_peers_down_requests, type_gauge)
		METRIC(peer, num_peers_end_game, type_gauge)
		METRIC(peer, num_peers_up_disk, type_gauge)
		METRIC(peer, num_peers_down_disk, type_gauge)

		// These counters count the number of times the
		// network thread wakes up for each respective
		// reason. If these counters are very large, it
		// may indicate a performance issue, causing the
		// network thread to wake up too ofte, wasting CPU.
		// mitigate it by increasing buffers and limits
		// for the specific trigger that wakes up the
		// thread.
		METRIC(net, on_read_counter, type_counter)
		METRIC(net, on_write_counter, type_counter)
		METRIC(net, on_tick_counter, type_counter)
		METRIC(net, on_lsd_counter, type_counter)
		METRIC(net, on_lsd_peer_counter, type_counter)
		METRIC(net, on_udp_counter, type_counter)
		METRIC(net, on_accept_counter, type_counter)
		METRIC(net, on_disk_counter, type_counter)

		// these gauges count the number of torrents in
		// different states. Each torrent only belongs to
		// one of these states. For torrents that could
		// belong to multiple of these, the most prominent
		// in picked. For instance, a torrent with an error
		// counts as an error-torrent, regardless of its other
		// state.
		METRIC(ses, num_checking_torrents, type_gauge)
		METRIC(ses, num_stopped_torrents, type_gauge)
		METRIC(ses, num_upload_only_torrents, type_gauge)
		METRIC(ses, num_downloading_torrents, type_gauge)
		METRIC(ses, num_seeding_torrents, type_gauge)
		METRIC(ses, num_queued_seeding_torrents, type_gauge)
		METRIC(ses, num_queued_download_torrents, type_gauge)
		METRIC(ses, num_error_torrents, type_gauge)

		// these count the number of times a piece has passed the
		// hash check, the number of times a piece was successfully
		// written to disk and the number of total possible pieces
		// added by adding torrents. e.g. when adding a torrent with
		// 1000 piece, num_total_pieces_added is incremented by 1000.
		// the *_removed version are incremented whenever the torrent
		// the pieces belong to was removed. The difference between
		// them represents the current number if pieces passed, haved
		// and total.
		METRIC(ses, num_piece_passed, type_counter)
		METRIC(ses, num_piece_passed_removed, type_counter)
		METRIC(ses, num_have_pieces, type_counter)
		METRIC(ses, num_have_pieces_removed, type_counter)
		METRIC(ses, num_total_pieces_added, type_counter)
		METRIC(ses, num_total_pieces_removed, type_counter)

		// this counts the number of times a torrent has been
		// evicted (only applies when `dynamic loading of torrent files`_
		// is enabled).
		METRIC(ses, torrent_evicted_counter, type_counter)

		// bittorrent message counters. These counters are incremented
		// every time a message of the corresponding type is received from
		// or sent to a bittorrent peer.
		METRIC(ses, num_incoming_choke, type_counter)
		METRIC(ses, num_incoming_unchoke, type_counter)
		METRIC(ses, num_incoming_interested, type_counter)
		METRIC(ses, num_incoming_not_interested, type_counter)
		METRIC(ses, num_incoming_have, type_counter)
		METRIC(ses, num_incoming_bitfield, type_counter)
		METRIC(ses, num_incoming_request, type_counter)
		METRIC(ses, num_incoming_piece, type_counter)
		METRIC(ses, num_incoming_cancel, type_counter)
		METRIC(ses, num_incoming_dht_port, type_counter)
		METRIC(ses, num_incoming_suggest, type_counter)
		METRIC(ses, num_incoming_have_all, type_counter)
		METRIC(ses, num_incoming_have_none, type_counter)
		METRIC(ses, num_incoming_reject, type_counter)
		METRIC(ses, num_incoming_allowed_fast, type_counter)
		METRIC(ses, num_incoming_ext_handshake, type_counter)
		METRIC(ses, num_incoming_pex, type_counter)
		METRIC(ses, num_incoming_metadata, type_counter)
		METRIC(ses, num_incoming_extended, type_counter)

		METRIC(ses, num_outgoing_choke, type_counter)
		METRIC(ses, num_outgoing_unchoke, type_counter)
		METRIC(ses, num_outgoing_interested, type_counter)
		METRIC(ses, num_outgoing_not_interested, type_counter)
		METRIC(ses, num_outgoing_have, type_counter)
		METRIC(ses, num_outgoing_bitfield, type_counter)
		METRIC(ses, num_outgoing_request, type_counter)
		METRIC(ses, num_outgoing_piece, type_counter)
		METRIC(ses, num_outgoing_cancel, type_counter)
		METRIC(ses, num_outgoing_dht_port, type_counter)
		METRIC(ses, num_outgoing_suggest, type_counter)
		METRIC(ses, num_outgoing_have_all, type_counter)
		METRIC(ses, num_outgoing_have_none, type_counter)
		METRIC(ses, num_outgoing_reject, type_counter)
		METRIC(ses, num_outgoing_allowed_fast, type_counter)
		METRIC(ses, num_outgoing_ext_handshake, type_counter)
		METRIC(ses, num_outgoing_pex, type_counter)
		METRIC(ses, num_outgoing_metadata, type_counter)
		METRIC(ses, num_outgoing_extended, type_counter)

		// counts the number of times the piece picker has been invoked
		METRIC(picker, piece_picks, type_counter)

		// the number of pieces considered while picking pieces
		METRIC(picker, piece_picker_loops, type_counter)

		// This breaks down the piece picks into the event that
		// triggered it
		METRIC(picker, end_game_piece_picker_blocks, type_counter)
		METRIC(picker, piece_picker_blocks, type_counter)
		METRIC(picker, reject_piece_picks, type_counter)
		METRIC(picker, unchoke_piece_picks, type_counter)
		METRIC(picker, incoming_redundant_piece_picks, type_counter)
		METRIC(picker, incoming_piece_picks, type_counter)
		METRIC(picker, end_game_piece_picks, type_counter)
		METRIC(picker, snubbed_piece_picks, type_counter)

		// ... more
	};
#undef METRIC

	void get_stats_metric_map(std::vector<stats_metric>& stats)
	{
		stats.resize(sizeof(metrics)/sizeof(metrics[0]));
		memcpy(&stats[0], metrics, sizeof(metrics));
	}
}

