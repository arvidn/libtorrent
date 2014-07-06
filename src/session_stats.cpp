/*

Copyright (c) 2012-2013, Arvid Norberg
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

#include "libtorrent/session.hpp" // for stats_metric
#include "libtorrent/aux_/session_interface.hpp" // for stats counter names
#include "libtorrent/performance_counters.hpp" // for counters
#include <boost/bind.hpp>

namespace libtorrent
{

	int find_metric_idx(std::vector<stats_metric> const& metrics
		, char const* name)
	{
		std::vector<stats_metric>::const_iterator i = std::find_if(metrics.begin()
			, metrics.end(), boost::bind(&strcmp
				, boost::bind(&stats_metric::name, _1), name) == 0);
		if (i == metrics.end()) return -1;
		return i->value_index;
	}

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
		METRIC(peer, connection_attempt_loops, type_counter)
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

		// total number of bytes sent and received by the session
		METRIC(net, sent_payload_bytes, type_counter)
		METRIC(net, sent_bytes, type_counter)
		METRIC(net, sent_ip_overhead_bytes, type_counter)
		METRIC(net, sent_tracker_bytes, type_counter)
		METRIC(net, recv_payload_bytes, type_counter)
		METRIC(net, recv_bytes, type_counter)
		METRIC(net, recv_ip_overhead_bytes, type_counter)
		METRIC(net, recv_tracker_bytes, type_counter)

		// the number of sockets currently waiting for upload and download
		// bandwidht from the rate limiter.
		METRIC(net, limiter_up_queue, type_gauge)
		METRIC(net, limiter_down_queue, type_gauge)

		// the number of upload and download bytes waiting to be handed out from
		// the rate limiter.
		METRIC(net, limiter_up_bytes, type_gauge)
		METRIC(net, limiter_down_bytes, type_gauge)

		// the number of bytes downloaded that had to be discarded because they
		// failed the hash check
		METRIC(net, recv_failed_bytes, type_counter)

		// the number of downloaded bytes that were discarded because they
		// were downloaded multiple times (from different peers)
		METRIC(net, recv_redundant_bytes, type_counter)

		// is false by default and set to true when
		// the first incoming connection is established
		// this is used to know if the client is behind
		// NAT or not.
		METRIC(net, has_incoming_connections, type_gauge)

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

		// the number of torrents that are currently loaded
		METRIC(ses, num_loaded_torrents, type_gauge)
		METRIC(ses, num_pinned_torrents, type_gauge)

		// these count the number of times a piece has passed the
		// hash check, the number of times a piece was successfully
		// written to disk and the number of total possible pieces
		// added by adding torrents. e.g. when adding a torrent with
		// 1000 piece, num_total_pieces_added is incremented by 1000.
		METRIC(ses, num_piece_passed, type_counter)
		METRIC(ses, num_piece_failed, type_counter)

		METRIC(ses, num_have_pieces, type_counter)
		METRIC(ses, num_total_pieces_added, type_counter)

		// this counts the number of times a torrent has been
		// evicted (only applies when `dynamic loading of torrent files`_
		// is enabled).
		METRIC(ses, torrent_evicted_counter, type_counter)

		// the number of allowed unchoked peers
		METRIC(peer, num_unchoke_slots, type_gauge)

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

		// the number of pieces considered while picking pieces
		METRIC(picker, piece_picker_partial_loops, type_counter)
		METRIC(picker, piece_picker_suggest_loops, type_counter)
		METRIC(picker, piece_picker_sequential_loops, type_counter)
		METRIC(picker, piece_picker_reverse_rare_loops, type_counter)
		METRIC(picker, piece_picker_rare_loops, type_counter)
		METRIC(picker, piece_picker_rand_start_loops, type_counter)
		METRIC(picker, piece_picker_rand_loops, type_counter)
		METRIC(picker, piece_picker_busy_loops, type_counter)

		// This breaks down the piece picks into the event that
		// triggered it
		METRIC(picker, reject_piece_picks, type_counter)
		METRIC(picker, unchoke_piece_picks, type_counter)
		METRIC(picker, incoming_redundant_piece_picks, type_counter)
		METRIC(picker, incoming_piece_picks, type_counter)
		METRIC(picker, end_game_piece_picks, type_counter)
		METRIC(picker, snubbed_piece_picks, type_counter)
		METRIC(picker, interesting_piece_picks, type_counter)
		METRIC(picker, hash_fail_piece_picks, type_counter)

		METRIC(disk, write_cache_blocks, type_gauge)
		METRIC(disk, read_cache_blocks, type_gauge)
		METRIC(disk, pinned_blocks, type_gauge)
		METRIC(disk, disk_blocks_in_use, type_gauge)
		METRIC(disk, queued_disk_jobs, type_gauge)
		METRIC(disk, num_read_jobs, type_gauge)
		METRIC(disk, num_write_jobs, type_gauge)
		METRIC(disk, num_jobs, type_gauge)
		METRIC(disk, num_writing_threads, type_gauge)
		METRIC(disk, num_running_threads, type_gauge)
		METRIC(disk, blocked_disk_jobs, type_gauge)

		// the number of bytes we have sent to the disk I/O
		// thread for writing. Every time we hear back from
		// the disk I/O thread with a completed write job, this
		// is updated to the number of bytes the disk I/O thread
		// is actually waiting for to be written (as opposed to
		// bytes just hanging out in the cache)
		METRIC(disk, queued_write_bytes, type_gauge)
		METRIC(disk, arc_mru_size, type_gauge)
		METRIC(disk, arc_mru_ghost_size, type_gauge)
		METRIC(disk, arc_mfu_size, type_gauge)
		METRIC(disk, arc_mfu_ghost_size, type_gauge)
		METRIC(disk, arc_write_size, type_gauge)
		METRIC(disk, arc_volatile_size, type_gauge)

		METRIC(disk, num_blocks_written, type_counter)
		METRIC(disk, num_blocks_read, type_counter)
		METRIC(disk, num_blocks_cache_hits, type_counter)
		METRIC(disk, num_write_ops, type_counter)
		METRIC(disk, num_read_ops, type_counter)

		// cumulative time spent in various disk jobs, as well
		// as total for all disk jobs. Measured in microseconds
		METRIC(disk, disk_read_time, type_counter)
		METRIC(disk, disk_write_time, type_counter)
		METRIC(disk, disk_hash_time, type_counter)
		METRIC(disk, disk_job_time, type_counter)

		// The number of nodes in the DHT routing table
		METRIC(dht, dht_nodes, type_gauge)

		// The number of replacement nodes in the DHT routing table
		METRIC(dht, dht_node_cache, type_gauge)

		// the number of torrents currently tracked by our DHT node
		METRIC(dht, dht_torrents, type_gauge)

		// the number of peers currently tracked by our DHT node
		METRIC(dht, dht_peers, type_gauge)

		// the number of immutable data items tracked by our DHT node
		METRIC(dht, dht_immutable_data, type_gauge)

		// the number of mutable data items tracked by our DHT node
		METRIC(dht, dht_mutable_data, type_gauge)

		// the number of RPC observers currently allocated
		METRIC(dht, dht_allocated_observers, type_gauge)

		// the total number of DHT messages sent and received
		METRIC(dht, dht_messages_in, type_counter)
		METRIC(dht, dht_messages_out, type_counter)

		// the number of outgoing messages that failed to be
		// sent
		METRIC(dht, dht_messages_out_dropped, type_counter)

		// the total number of bytes sent and received by the DHT
		METRIC(dht, dht_bytes_in, type_counter)
		METRIC(dht, dht_bytes_out, type_counter)

		// the number of DHT messages we've sent and received
		// by kind.
		METRIC(dht, dht_ping_in, type_counter)
		METRIC(dht, dht_ping_out, type_counter)
		METRIC(dht, dht_find_node_in, type_counter)
		METRIC(dht, dht_find_node_out, type_counter)
		METRIC(dht, dht_get_peers_in, type_counter)
		METRIC(dht, dht_get_peers_out, type_counter)
		METRIC(dht, dht_announce_peer_in, type_counter)
		METRIC(dht, dht_announce_peer_out, type_counter)
		METRIC(dht, dht_get_in, type_counter)
		METRIC(dht, dht_get_out, type_counter)
		METRIC(dht, dht_put_in, type_counter)
		METRIC(dht, dht_put_out, type_counter)

		// uTP counters
		METRIC(utp, utp_packet_loss, type_counter)
		METRIC(utp, utp_timeout, type_counter)
		METRIC(utp, utp_packets_in, type_counter)
		METRIC(utp, utp_packets_out, type_counter)
		METRIC(utp, utp_fast_retransmit, type_counter)
		METRIC(utp, utp_packet_resend, type_counter)
		METRIC(utp, utp_samples_above_target, type_counter)
		METRIC(utp, utp_samples_below_target, type_counter)
		METRIC(utp, utp_payload_pkts_in, type_counter)
		METRIC(utp, utp_payload_pkts_out, type_counter)
		METRIC(utp, utp_invalid_pkts_in, type_counter)
		METRIC(utp, utp_redundant_pkts_in, type_counter)

		// the buffer sizes accepted by
		// socket send and receive calls respectively.
		// The larger the buffers are, the more efficient,
		// because it reqire fewer system calls per byte.
		// The size is 1 << n, where n is the number
		// at the end of the counter name. i.e.
		// 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
		// 16384, 32768, 65536, 131072, 262144, 524288, 1048576
		// bytes
		METRIC(sock_bufs, socket_send_size3, type_counter)
		METRIC(sock_bufs, socket_send_size4, type_counter)
		METRIC(sock_bufs, socket_send_size5, type_counter)
		METRIC(sock_bufs, socket_send_size6, type_counter)
		METRIC(sock_bufs, socket_send_size7, type_counter)
		METRIC(sock_bufs, socket_send_size8, type_counter)
		METRIC(sock_bufs, socket_send_size9, type_counter)
		METRIC(sock_bufs, socket_send_size10, type_counter)
		METRIC(sock_bufs, socket_send_size11, type_counter)
		METRIC(sock_bufs, socket_send_size12, type_counter)
		METRIC(sock_bufs, socket_send_size13, type_counter)
		METRIC(sock_bufs, socket_send_size14, type_counter)
		METRIC(sock_bufs, socket_send_size15, type_counter)
		METRIC(sock_bufs, socket_send_size16, type_counter)
		METRIC(sock_bufs, socket_send_size17, type_counter)
		METRIC(sock_bufs, socket_send_size18, type_counter)
		METRIC(sock_bufs, socket_send_size19, type_counter)
		METRIC(sock_bufs, socket_send_size20, type_counter)
		METRIC(sock_bufs, socket_recv_size3, type_counter)
		METRIC(sock_bufs, socket_recv_size4, type_counter)
		METRIC(sock_bufs, socket_recv_size5, type_counter)
		METRIC(sock_bufs, socket_recv_size6, type_counter)
		METRIC(sock_bufs, socket_recv_size7, type_counter)
		METRIC(sock_bufs, socket_recv_size8, type_counter)
		METRIC(sock_bufs, socket_recv_size9, type_counter)
		METRIC(sock_bufs, socket_recv_size10, type_counter)
		METRIC(sock_bufs, socket_recv_size11, type_counter)
		METRIC(sock_bufs, socket_recv_size12, type_counter)
		METRIC(sock_bufs, socket_recv_size13, type_counter)
		METRIC(sock_bufs, socket_recv_size14, type_counter)
		METRIC(sock_bufs, socket_recv_size15, type_counter)
		METRIC(sock_bufs, socket_recv_size16, type_counter)
		METRIC(sock_bufs, socket_recv_size17, type_counter)
		METRIC(sock_bufs, socket_recv_size18, type_counter)
		METRIC(sock_bufs, socket_recv_size19, type_counter)
		METRIC(sock_bufs, socket_recv_size20, type_counter)

		// ... more
	};
#undef METRIC

	void get_stats_metric_map(std::vector<stats_metric>& stats)
	{
		stats.resize(sizeof(metrics)/sizeof(metrics[0]));
		memcpy(&stats[0], metrics, sizeof(metrics));
	}
}

