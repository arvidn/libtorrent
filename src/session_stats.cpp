/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2019, Steven Siloti
Copyright (c) 2020, FranciscoPombal
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

#include "libtorrent/session_stats.hpp" // for stats_metric
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/performance_counters.hpp" // for counters

#include <cstring>
#include <algorithm>

namespace libtorrent {

#if TORRENT_ABI_VERSION == 1
	constexpr metric_type_t stats_metric::type_counter;
	constexpr metric_type_t stats_metric::type_gauge;
#endif

namespace {

	struct stats_metric_impl
	{
		char const* name;
		int value_index;
	};

#define METRIC(category, name) { #category "." #name, counters:: name },
	aux::array<stats_metric_impl, counters::num_counters> const metrics
	({{
		// ``error_peers`` is the total number of peer disconnects
		// caused by an error (not initiated by this client) and
		// disconnected initiated by this client (``disconnected_peers``).
		METRIC(peer, error_peers)
		METRIC(peer, disconnected_peers)

		// these counters break down the peer errors into more specific
		// categories. These errors are what the underlying transport
		// reported (i.e. TCP or uTP)
		METRIC(peer, eof_peers)
		METRIC(peer, connreset_peers)
		METRIC(peer, connrefused_peers)
		METRIC(peer, connaborted_peers)
		METRIC(peer, notconnected_peers)
		METRIC(peer, perm_peers)
		METRIC(peer, buffer_peers)
		METRIC(peer, unreachable_peers)
		METRIC(peer, broken_pipe_peers)
		METRIC(peer, addrinuse_peers)
		METRIC(peer, no_access_peers)
		METRIC(peer, invalid_arg_peers)
		METRIC(peer, aborted_peers)

		// the total number of incoming piece requests we've received followed
		// by the number of rejected piece requests for various reasons.
		// max_piece_requests mean we already had too many outstanding requests
		// from this peer, so we rejected it. cancelled_piece_requests are ones
		// where the other end explicitly asked for the piece to be rejected.
		METRIC(peer, piece_requests)
		METRIC(peer, max_piece_requests)
		METRIC(peer, invalid_piece_requests)
		METRIC(peer, choked_piece_requests)
		METRIC(peer, cancelled_piece_requests)
		METRIC(peer, piece_rejects)

		// these counters break down the peer errors into
		// whether they happen on incoming or outgoing peers.
		METRIC(peer, error_incoming_peers)
		METRIC(peer, error_outgoing_peers)

		// these counters break down the peer errors into
		// whether they happen on encrypted peers (just
		// encrypted handshake) and rc4 peers (full stream
		// encryption). These can indicate whether encrypted
		// peers are more or less likely to fail
		METRIC(peer, error_rc4_peers)
		METRIC(peer, error_encrypted_peers)

		// these counters break down the peer errors into
		// whether they happen on uTP peers or TCP peers.
		// these may indicate whether one protocol is
		// more error prone
		METRIC(peer, error_tcp_peers)
		METRIC(peer, error_utp_peers)

		// these counters break down the reasons to
		// disconnect peers.
		METRIC(peer, connect_timeouts)
		METRIC(peer, uninteresting_peers)
		METRIC(peer, timeout_peers)
		METRIC(peer, no_memory_peers)
		METRIC(peer, too_many_peers)
		METRIC(peer, transport_timeout_peers)
		METRIC(peer, num_banned_peers)
		METRIC(peer, banned_for_hash_failure)

		METRIC(peer, connection_attempts)
		METRIC(peer, connection_attempt_loops)
		METRIC(peer, boost_connection_attempts)
		METRIC(peer, missed_connection_attempts)
		METRIC(peer, no_peer_connection_attempts)
		METRIC(peer, incoming_connections)

		// the number of peer connections for each kind of socket.
		// ``num_peers_half_open`` counts half-open (connecting) peers, no other
		// count includes those peers.
		// ``num_peers_up_unchoked_all`` is the total number of unchoked peers,
		// whereas ``num_peers_up_unchoked`` only are unchoked peers that count
		// against the limit (i.e. excluding peers that are unchoked because the
		// limit doesn't apply to them). ``num_peers_up_unchoked_optimistic`` is
		// the number of optimistically unchoked peers.
		METRIC(peer, num_tcp_peers)
		METRIC(peer, num_socks5_peers)
		METRIC(peer, num_http_proxy_peers)
		METRIC(peer, num_utp_peers)
		METRIC(peer, num_i2p_peers)
		METRIC(peer, num_ssl_peers)
		METRIC(peer, num_ssl_socks5_peers)
		METRIC(peer, num_ssl_http_proxy_peers)
		METRIC(peer, num_ssl_utp_peers)

		METRIC(peer, num_peers_half_open)
		METRIC(peer, num_peers_connected)
		METRIC(peer, num_peers_up_interested)
		METRIC(peer, num_peers_down_interested)
		METRIC(peer, num_peers_up_unchoked_all)
		METRIC(peer, num_peers_up_unchoked_optimistic)
		METRIC(peer, num_peers_up_unchoked)
		METRIC(peer, num_peers_down_unchoked)
		METRIC(peer, num_peers_up_requests)
		METRIC(peer, num_peers_down_requests)
		METRIC(peer, num_peers_end_game)
		METRIC(peer, num_peers_up_disk)
		METRIC(peer, num_peers_down_disk)

		// These counters count the number of times the
		// network thread wakes up for each respective
		// reason. If these counters are very large, it
		// may indicate a performance issue, causing the
		// network thread to wake up too ofte, wasting CPU.
		// mitigate it by increasing buffers and limits
		// for the specific trigger that wakes up the
		// thread.
		METRIC(net, on_read_counter)
		METRIC(net, on_write_counter)
		METRIC(net, on_tick_counter)
		METRIC(net, on_lsd_counter)
		METRIC(net, on_lsd_peer_counter)
		METRIC(net, on_udp_counter)
		METRIC(net, on_accept_counter)
		METRIC(net, on_disk_queue_counter)
		METRIC(net, on_disk_counter)

		// total number of bytes sent and received by the session
		METRIC(net, sent_payload_bytes)
		METRIC(net, sent_bytes)
		METRIC(net, sent_ip_overhead_bytes)
		METRIC(net, sent_tracker_bytes)
		METRIC(net, recv_payload_bytes)
		METRIC(net, recv_bytes)
		METRIC(net, recv_ip_overhead_bytes)
		METRIC(net, recv_tracker_bytes)

		// the number of sockets currently waiting for upload and download
		// bandwidth from the rate limiter.
		METRIC(net, limiter_up_queue)
		METRIC(net, limiter_down_queue)

		// the number of upload and download bytes waiting to be handed out from
		// the rate limiter.
		METRIC(net, limiter_up_bytes)
		METRIC(net, limiter_down_bytes)

		// the number of bytes downloaded that had to be discarded because they
		// failed the hash check
		METRIC(net, recv_failed_bytes)

		// the number of downloaded bytes that were discarded because they
		// were downloaded multiple times (from different peers)
		METRIC(net, recv_redundant_bytes)

		// is false by default and set to true when
		// the first incoming connection is established
		// this is used to know if the client is behind
		// NAT or not.
		METRIC(net, has_incoming_connections)

		// these gauges count the number of torrents in
		// different states. Each torrent only belongs to
		// one of these states. For torrents that could
		// belong to multiple of these, the most prominent
		// in picked. For instance, a torrent with an error
		// counts as an error-torrent, regardless of its other
		// state.
		METRIC(ses, num_checking_torrents)
		METRIC(ses, num_stopped_torrents)
		METRIC(ses, num_upload_only_torrents)
		METRIC(ses, num_downloading_torrents)
		METRIC(ses, num_seeding_torrents)
		METRIC(ses, num_queued_seeding_torrents)
		METRIC(ses, num_queued_download_torrents)
		METRIC(ses, num_error_torrents)

		// the number of torrents that don't have the
		// IP filter applied to them.
		METRIC(ses, non_filter_torrents)

		// these count the number of times a piece has passed the
		// hash check, the number of times a piece was successfully
		// written to disk and the number of total possible pieces
		// added by adding torrents. e.g. when adding a torrent with
		// 1000 piece, num_total_pieces_added is incremented by 1000.
		METRIC(ses, num_piece_passed)
		METRIC(ses, num_piece_failed)

		METRIC(ses, num_have_pieces)
		METRIC(ses, num_total_pieces_added)

		// the number of allowed unchoked peers
		METRIC(ses, num_unchoke_slots)

		// the number of listen sockets that are currently accepting incoming
		// connections
		METRIC(ses, num_outstanding_accept)

		// bittorrent message counters. These counters are incremented
		// every time a message of the corresponding type is received from
		// or sent to a bittorrent peer.
		METRIC(ses, num_incoming_choke)
		METRIC(ses, num_incoming_unchoke)
		METRIC(ses, num_incoming_interested)
		METRIC(ses, num_incoming_not_interested)
		METRIC(ses, num_incoming_have)
		METRIC(ses, num_incoming_bitfield)
		METRIC(ses, num_incoming_request)
		METRIC(ses, num_incoming_piece)
		METRIC(ses, num_incoming_cancel)
		METRIC(ses, num_incoming_dht_port)
		METRIC(ses, num_incoming_suggest)
		METRIC(ses, num_incoming_have_all)
		METRIC(ses, num_incoming_have_none)
		METRIC(ses, num_incoming_reject)
		METRIC(ses, num_incoming_allowed_fast)
		METRIC(ses, num_incoming_ext_handshake)
		METRIC(ses, num_incoming_pex)
		METRIC(ses, num_incoming_metadata)
		METRIC(ses, num_incoming_extended)

		METRIC(ses, num_outgoing_choke)
		METRIC(ses, num_outgoing_unchoke)
		METRIC(ses, num_outgoing_interested)
		METRIC(ses, num_outgoing_not_interested)
		METRIC(ses, num_outgoing_have)
		METRIC(ses, num_outgoing_bitfield)
		METRIC(ses, num_outgoing_request)
		METRIC(ses, num_outgoing_piece)
		METRIC(ses, num_outgoing_cancel)
		METRIC(ses, num_outgoing_dht_port)
		METRIC(ses, num_outgoing_suggest)
		METRIC(ses, num_outgoing_have_all)
		METRIC(ses, num_outgoing_have_none)
		METRIC(ses, num_outgoing_reject)
		METRIC(ses, num_outgoing_allowed_fast)
		METRIC(ses, num_outgoing_ext_handshake)
		METRIC(ses, num_outgoing_pex)
		METRIC(ses, num_outgoing_metadata)
		METRIC(ses, num_outgoing_extended)
		METRIC(ses, num_outgoing_hash_request)
		METRIC(ses, num_outgoing_hashes)
		METRIC(ses, num_outgoing_hash_reject)

		// the number of wasted downloaded bytes by reason of the bytes being
		// wasted.
		METRIC(ses, waste_piece_timed_out)
		METRIC(ses, waste_piece_cancelled)
		METRIC(ses, waste_piece_unknown)
		METRIC(ses, waste_piece_seed)
		METRIC(ses, waste_piece_end_game)
		METRIC(ses, waste_piece_closing)

		// the number of pieces considered while picking pieces
		METRIC(picker, piece_picker_partial_loops)
		METRIC(picker, piece_picker_suggest_loops)
		METRIC(picker, piece_picker_sequential_loops)
		METRIC(picker, piece_picker_reverse_rare_loops)
		METRIC(picker, piece_picker_rare_loops)
		METRIC(picker, piece_picker_rand_start_loops)
		METRIC(picker, piece_picker_rand_loops)
		METRIC(picker, piece_picker_busy_loops)

		// This breaks down the piece picks into the event that
		// triggered it
		METRIC(picker, reject_piece_picks)
		METRIC(picker, unchoke_piece_picks)
		METRIC(picker, incoming_redundant_piece_picks)
		METRIC(picker, incoming_piece_picks)
		METRIC(picker, end_game_piece_picks)
		METRIC(picker, snubbed_piece_picks)
		METRIC(picker, interesting_piece_picks)
		METRIC(picker, hash_fail_piece_picks)

		// the number of microseconds it takes from receiving a request from a
		// peer until we're sending the response back on the socket.
		METRIC(disk, request_latency)

		METRIC(disk, disk_blocks_in_use)

		// ``queued_disk_jobs`` is the number of disk jobs currently queued,
		// waiting to be executed by a disk thread.
		METRIC(disk, queued_disk_jobs)
		METRIC(disk, num_running_disk_jobs)
		METRIC(disk, num_read_jobs)
		METRIC(disk, num_write_jobs)
		METRIC(disk, num_jobs)
		METRIC(disk, blocked_disk_jobs)

		METRIC(disk, num_writing_threads)
		METRIC(disk, num_running_threads)

		// the number of bytes we have sent to the disk I/O
		// thread for writing. Every time we hear back from
		// the disk I/O thread with a completed write job, this
		// is updated to the number of bytes the disk I/O thread
		// is actually waiting for to be written (as opposed to
		// bytes just hanging out in the cache)
		METRIC(disk, queued_write_bytes)

		// the number of blocks written and read from disk in total. A block is 16
		// kiB. ``num_blocks_written`` and ``num_blocks_read``
		METRIC(disk, num_blocks_written)
		METRIC(disk, num_blocks_read)

		// the total number of blocks run through SHA-1 hashing
		METRIC(disk, num_blocks_hashed)

		// the number of disk I/O operation for reads and writes. One disk
		// operation may transfer more then one block.
		METRIC(disk, num_write_ops)
		METRIC(disk, num_read_ops)

		// the number of blocks that had to be read back from disk in order to
		// hash a piece (when verifying against the piece hash)
		METRIC(disk, num_read_back)

		// cumulative time spent in various disk jobs, as well
		// as total for all disk jobs. Measured in microseconds
		METRIC(disk, disk_read_time)
		METRIC(disk, disk_write_time)
		METRIC(disk, disk_hash_time)
		METRIC(disk, disk_job_time)

		// for each kind of disk job, a counter of how many jobs of that kind
		// are currently blocked by a disk fence
		METRIC(disk, num_fenced_read)
		METRIC(disk, num_fenced_write)
		METRIC(disk, num_fenced_hash)
		METRIC(disk, num_fenced_move_storage)
		METRIC(disk, num_fenced_release_files)
		METRIC(disk, num_fenced_delete_files)
		METRIC(disk, num_fenced_check_fastresume)
		METRIC(disk, num_fenced_save_resume_data)
		METRIC(disk, num_fenced_rename_file)
		METRIC(disk, num_fenced_stop_torrent)
		METRIC(disk, num_fenced_flush_piece)
		METRIC(disk, num_fenced_flush_hashed)
		METRIC(disk, num_fenced_flush_storage)
		METRIC(disk, num_fenced_file_priority)
		METRIC(disk, num_fenced_load_torrent)
		METRIC(disk, num_fenced_clear_piece)
		METRIC(disk, num_fenced_tick_storage)

		// The number of nodes in the DHT routing table
		METRIC(dht, dht_nodes)

		// The number of replacement nodes in the DHT routing table
		METRIC(dht, dht_node_cache)

		// the number of torrents currently tracked by our DHT node
		METRIC(dht, dht_torrents)

		// the number of peers currently tracked by our DHT node
		METRIC(dht, dht_peers)

		// the number of immutable data items tracked by our DHT node
		METRIC(dht, dht_immutable_data)

		// the number of mutable data items tracked by our DHT node
		METRIC(dht, dht_mutable_data)

		// the number of RPC observers currently allocated
		METRIC(dht, dht_allocated_observers)

		// the total number of DHT messages sent and received
		METRIC(dht, dht_messages_in)
		METRIC(dht, dht_messages_out)

		// the number of incoming DHT requests that were dropped. There are a few
		// different reasons why incoming DHT packets may be dropped:
		//
		// 1. there wasn't enough send quota to respond to them.
		// 2. the Denial of service logic kicked in, blocking the peer
		// 3. ignore_dark_internet is enabled, and the packet came from a
		//    non-public IP address
		// 4. the bencoding of the message was invalid
		METRIC(dht, dht_messages_in_dropped)

		// the number of outgoing messages that failed to be
		// sent
		METRIC(dht, dht_messages_out_dropped)

		// the total number of bytes sent and received by the DHT
		METRIC(dht, dht_bytes_in)
		METRIC(dht, dht_bytes_out)

		// the number of DHT messages we've sent and received
		// by kind.
		METRIC(dht, dht_ping_in)
		METRIC(dht, dht_ping_out)
		METRIC(dht, dht_find_node_in)
		METRIC(dht, dht_find_node_out)
		METRIC(dht, dht_get_peers_in)
		METRIC(dht, dht_get_peers_out)
		METRIC(dht, dht_announce_peer_in)
		METRIC(dht, dht_announce_peer_out)
		METRIC(dht, dht_get_in)
		METRIC(dht, dht_get_out)
		METRIC(dht, dht_put_in)
		METRIC(dht, dht_put_out)
		METRIC(dht, dht_sample_infohashes_in)
		METRIC(dht, dht_sample_infohashes_out)

		// the number of failed incoming DHT requests by kind of request
		METRIC(dht, dht_invalid_announce)
		METRIC(dht, dht_invalid_get_peers)
		METRIC(dht, dht_invalid_find_node)
		METRIC(dht, dht_invalid_put)
		METRIC(dht, dht_invalid_get)
		METRIC(dht, dht_invalid_sample_infohashes)

		// The number of times a lost packet has been interpreted as congestion,
		// cutting the congestion window in half. Some lost packets are not
		// interpreted as congestion, notably MTU-probes
		METRIC(utp, utp_packet_loss)

		// The number of timeouts experienced. This is when a connection doesn't
		// hear back from the other end within a sliding average RTT + 2 average
		// deviations from the mean (approximately). The actual time out is
		// configurable and also depends on the state of the socket.
		METRIC(utp, utp_timeout)

		// The total number of packets sent and received
		METRIC(utp, utp_packets_in)
		METRIC(utp, utp_packets_out)

		// The number of packets lost but re-sent by the fast-retransmit logic.
		// This logic is triggered after 3 duplicate ACKs.
		METRIC(utp, utp_fast_retransmit)

		// The number of packets that were re-sent, for whatever reason
		METRIC(utp, utp_packet_resend)

		// The number of incoming packets where the delay samples were above
		// and below the delay target, respectively. The delay target is
		// configurable and is a parameter to the LEDBAT congestion control.
		METRIC(utp, utp_samples_above_target)
		METRIC(utp, utp_samples_below_target)

		// The total number of packets carrying payload received and sent,
		// respectively.
		METRIC(utp, utp_payload_pkts_in)
		METRIC(utp, utp_payload_pkts_out)

		// The number of packets received that are not valid uTP packets (but
		// were sufficiently similar to not be treated as DHT or UDP tracker
		// packets).
		METRIC(utp, utp_invalid_pkts_in)

		// The number of duplicate payload packets received. This may happen if
		// the outgoing ACK is lost.
		METRIC(utp, utp_redundant_pkts_in)

		// the number of uTP sockets in each respective state
		METRIC(utp, num_utp_idle)
		METRIC(utp, num_utp_syn_sent)
		METRIC(utp, num_utp_connected)
		METRIC(utp, num_utp_fin_sent)
		METRIC(utp, num_utp_close_wait)
		METRIC(utp, num_utp_deleted)

		// the buffer sizes accepted by
		// socket send and receive calls respectively.
		// The larger the buffers are, the more efficient,
		// because it reqire fewer system calls per byte.
		// The size is 1 << n, where n is the number
		// at the end of the counter name. i.e.
		// 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
		// 16384, 32768, 65536, 131072, 262144, 524288, 1048576
		// bytes
		METRIC(sock_bufs, socket_send_size3)
		METRIC(sock_bufs, socket_send_size4)
		METRIC(sock_bufs, socket_send_size5)
		METRIC(sock_bufs, socket_send_size6)
		METRIC(sock_bufs, socket_send_size7)
		METRIC(sock_bufs, socket_send_size8)
		METRIC(sock_bufs, socket_send_size9)
		METRIC(sock_bufs, socket_send_size10)
		METRIC(sock_bufs, socket_send_size11)
		METRIC(sock_bufs, socket_send_size12)
		METRIC(sock_bufs, socket_send_size13)
		METRIC(sock_bufs, socket_send_size14)
		METRIC(sock_bufs, socket_send_size15)
		METRIC(sock_bufs, socket_send_size16)
		METRIC(sock_bufs, socket_send_size17)
		METRIC(sock_bufs, socket_send_size18)
		METRIC(sock_bufs, socket_send_size19)
		METRIC(sock_bufs, socket_send_size20)
		METRIC(sock_bufs, socket_recv_size3)
		METRIC(sock_bufs, socket_recv_size4)
		METRIC(sock_bufs, socket_recv_size5)
		METRIC(sock_bufs, socket_recv_size6)
		METRIC(sock_bufs, socket_recv_size7)
		METRIC(sock_bufs, socket_recv_size8)
		METRIC(sock_bufs, socket_recv_size9)
		METRIC(sock_bufs, socket_recv_size10)
		METRIC(sock_bufs, socket_recv_size11)
		METRIC(sock_bufs, socket_recv_size12)
		METRIC(sock_bufs, socket_recv_size13)
		METRIC(sock_bufs, socket_recv_size14)
		METRIC(sock_bufs, socket_recv_size15)
		METRIC(sock_bufs, socket_recv_size16)
		METRIC(sock_bufs, socket_recv_size17)
		METRIC(sock_bufs, socket_recv_size18)
		METRIC(sock_bufs, socket_recv_size19)
		METRIC(sock_bufs, socket_recv_size20)

		// if the outstanding tracker announce limit is reached, tracker
		// announces are queued, to be issued when an announce slot opens up.
		// this measure the number of tracker announces currently in the
		// queue
		METRIC(tracker, num_queued_tracker_announces)
		// ... more
	}});
#undef METRIC
	} // anonymous namespace

	std::vector<stats_metric> session_stats_metrics()
	{
		aux::vector<stats_metric> stats;
		stats.resize(metrics.size());
		for (int i = 0; i < metrics.end_index(); ++i)
		{
			stats[i].name = metrics[i].name;
			stats[i].value_index = metrics[i].value_index;
			stats[i].type = metrics[i].value_index >= counters::num_stats_counters
				? metric_type_t::gauge : metric_type_t::counter;
		}
		return std::move(stats);
	}

	int find_metric_idx(string_view name)
	{
		auto const i = std::find_if(std::begin(metrics), std::end(metrics)
			, [name](stats_metric_impl const& metr)
			{ return metr.name == name; });

		if (i == std::end(metrics)) return -1;
		return i->value_index;
	}
}
