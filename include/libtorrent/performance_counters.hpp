/*

Copyright (c) 2013-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/array.hpp"

#include <cstdint>
#include <atomic>
#include <mutex>

namespace libtorrent {

	struct TORRENT_EXTRA_EXPORT counters
	{
		// TODO: move this out of counters
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
			notconnected_peers,
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

			// the number of times the piece picker was
			// successfully invoked, split by the reason
			// it was invoked
			reject_piece_picks,
			unchoke_piece_picks,
			incoming_redundant_piece_picks,
			incoming_piece_picks,
			end_game_piece_picks,
			snubbed_piece_picks,
			interesting_piece_picks,
			hash_fail_piece_picks,

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
			// the number of iterations over the peer list when finding
			// a connect candidate
			connection_attempt_loops,

			// the number of peer connection attempts made as high
			// priority connections for new torrents
			boost_connection_attempts,

			// calls to torrent::connect_to_peer() that failed
			missed_connection_attempts,

			// calls to peer_list::connect_one_peer() resulting in
			// no peer candidate being found
			no_peer_connection_attempts,

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

#if TORRENT_ABI_VERSION == 1
			torrent_evicted_counter,
#endif

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

			num_have_pieces,
			num_total_pieces_added,

			num_blocks_written,
			num_blocks_read,
			num_blocks_hashed,
			num_blocks_cache_hits,
			num_write_ops,
			num_read_ops,
			num_read_back,

			disk_read_time,
			disk_write_time,
			disk_hash_time,
			disk_job_time,

			waste_piece_timed_out,
			waste_piece_cancelled,
			waste_piece_unknown,
			waste_piece_seed,
			waste_piece_end_game,
			waste_piece_closing,

			sent_payload_bytes,
			sent_bytes,
			sent_ip_overhead_bytes,
			sent_tracker_bytes,
			recv_payload_bytes,
			recv_bytes,
			recv_ip_overhead_bytes,
			recv_tracker_bytes,

			recv_failed_bytes,
			recv_redundant_bytes,

			dht_messages_in,
			dht_messages_in_dropped,
			dht_messages_out,
			dht_messages_out_dropped,
			dht_bytes_in,
			dht_bytes_out,

			dht_ping_in,
			dht_ping_out,
			dht_find_node_in,
			dht_find_node_out,
			dht_get_peers_in,
			dht_get_peers_out,
			dht_announce_peer_in,
			dht_announce_peer_out,
			dht_get_in,
			dht_get_out,
			dht_put_in,
			dht_put_out,
			dht_sample_infohashes_in,
			dht_sample_infohashes_out,

			dht_invalid_announce,
			dht_invalid_get_peers,
			dht_invalid_find_node,
			dht_invalid_put,
			dht_invalid_get,
			dht_invalid_sample_infohashes,

			// uTP counters.
			utp_packet_loss,
			utp_timeout,
			utp_packets_in,
			utp_packets_out,
			utp_fast_retransmit,
			utp_packet_resend,
			utp_samples_above_target,
			utp_samples_below_target,
			utp_payload_pkts_in,
			utp_payload_pkts_out,
			utp_invalid_pkts_in,
			utp_redundant_pkts_in,

			// the buffer sizes accepted by
			// socket send calls. The larger
			// the more efficient. The size is
			// 1 << n, where n is the number
			// at the end of the counter name

			// 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
			// 16384, 32768, 65536, 131072, 262144, 524288, 1048576
			socket_send_size3,
			socket_send_size4,
			socket_send_size5,
			socket_send_size6,
			socket_send_size7,
			socket_send_size8,
			socket_send_size9,
			socket_send_size10,
			socket_send_size11,
			socket_send_size12,
			socket_send_size13,
			socket_send_size14,
			socket_send_size15,
			socket_send_size16,
			socket_send_size17,
			socket_send_size18,
			socket_send_size19,
			socket_send_size20,

			// the buffer sizes returned by
			// socket recv calls. The larger
			// the more efficient. The size is
			// 1 << n, where n is the number
			// at the end of the counter name

			// 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
			// 16384, 32768, 65536, 131072, 262144, 524288, 1048576
			socket_recv_size3,
			socket_recv_size4,
			socket_recv_size5,
			socket_recv_size6,
			socket_recv_size7,
			socket_recv_size8,
			socket_recv_size9,
			socket_recv_size10,
			socket_recv_size11,
			socket_recv_size12,
			socket_recv_size13,
			socket_recv_size14,
			socket_recv_size15,
			socket_recv_size16,
			socket_recv_size17,
			socket_recv_size18,
			socket_recv_size19,
			socket_recv_size20,

			num_stats_counters
		};

		// == ALL FOLLOWING ARE GAUGES ==

		// it is important that all gauges have a higher index than counters.
		// This assumption is relied upon in other parts of the code
		enum stats_gauge_t
		{
			num_checking_torrents = num_stats_counters,
			num_stopped_torrents,
			num_upload_only_torrents, // upload_only means finished
			num_downloading_torrents,
			num_seeding_torrents,
			num_queued_seeding_torrents,
			num_queued_download_torrents,
			num_error_torrents,

			// the number of torrents that don't have the
			// IP filter applied to them.
			non_filter_torrents,

			// these counter indices deliberately
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

			// the number of peer connections that are half-open (i.e. in the
			// process of completing a connection attempt) and fully connected.
			// These states are mutually exclusive (a connection cannot be in both
			// states simultaneously).
			num_peers_half_open,
			num_peers_connected,

			// the number of peers interested in us (``up_interested``) and peers
			// we are interested in (``down_interested``).
			num_peers_up_interested,
			num_peers_down_interested,

			// the total number of unchoked peers (``up_unchoked_all``), the number
			// of peers unchoked via the optimistic unchoke
			// (``up_unchoked_optimistic``) and peers unchoked via the
			// reciprocation (regular) unchoke mechanism (``up_unchoked``).
			// and the number of peers that have unchoked us (``down_unchoked).
			num_peers_up_unchoked_all,
			num_peers_up_unchoked_optimistic,
			num_peers_up_unchoked,
			num_peers_down_unchoked,

			// the number of peers with at least one piece request pending,
			// downloading (``down_requests``) or uploading (``up_requests``)
			num_peers_up_requests,
			num_peers_down_requests,

			// the number of peers that have at least one outstanding disk request,
			// either reading (``up_disk``) or writing (``down_disk``).
			num_peers_up_disk,
			num_peers_down_disk,

			// the number of peers in end-game mode. End game mode is where there
			// are no blocks that we have not sent any requests to download. In ths
			// mode, blocks are allowed to be requested from more than one peer at
			// at time.
			num_peers_end_game,

			write_cache_blocks,
			read_cache_blocks,
			request_latency,
			pinned_blocks,
			disk_blocks_in_use,
			queued_disk_jobs,
			num_running_disk_jobs,
			num_read_jobs,
			num_write_jobs,
			num_jobs,
			num_writing_threads,
			num_running_threads,
			blocked_disk_jobs,
			queued_write_bytes,
			num_unchoke_slots,

			num_fenced_read,
			num_fenced_write,
			num_fenced_hash,
			num_fenced_move_storage,
			num_fenced_release_files,
			num_fenced_delete_files,
			num_fenced_check_fastresume,
			num_fenced_save_resume_data,
			num_fenced_rename_file,
			num_fenced_stop_torrent,
			num_fenced_flush_piece,
			num_fenced_flush_hashed,
			num_fenced_flush_storage,
			num_fenced_trim_cache,
			num_fenced_file_priority,
			num_fenced_load_torrent,
			num_fenced_clear_piece,
			num_fenced_tick_storage,

			arc_mru_size,
			arc_mru_ghost_size,
			arc_mfu_size,
			arc_mfu_ghost_size,
			arc_write_size,
			arc_volatile_size,

			dht_nodes,
			dht_node_cache,
			dht_torrents,
			dht_peers,
			dht_immutable_data,
			dht_mutable_data,
			dht_allocated_observers,

			has_incoming_connections,

			limiter_up_queue,
			limiter_down_queue,
			limiter_up_bytes,
			limiter_down_bytes,

			// the number of uTP connections in each respective state
			// these must be defined in the same order as the state_t enum
			// in utp_stream
			num_utp_idle,
			num_utp_syn_sent,
			num_utp_connected,
			num_utp_fin_sent,
			num_utp_close_wait,
			num_utp_deleted,

			num_outstanding_accept,

			num_counters,
			num_gauges_counters = num_counters - num_stats_counters
		};
#ifdef ATOMIC_LLONG_LOCK_FREE
#define TORRENT_COUNTER_NOEXCEPT noexcept
#else
#define TORRENT_COUNTER_NOEXCEPT
#endif

		counters() TORRENT_COUNTER_NOEXCEPT;

		counters(counters const&) TORRENT_COUNTER_NOEXCEPT;
		counters& operator=(counters const&) TORRENT_COUNTER_NOEXCEPT;

		// returns the new value
		std::int64_t inc_stats_counter(int c, std::int64_t value = 1) TORRENT_COUNTER_NOEXCEPT;
		std::int64_t operator[](int i) const TORRENT_COUNTER_NOEXCEPT;

		void set_value(int c, std::int64_t value) TORRENT_COUNTER_NOEXCEPT;
		void blend_stats_counter(int c, std::int64_t value, int ratio) TORRENT_COUNTER_NOEXCEPT;

	private:

		// TODO: some space could be saved here by making gauges 32 bits
		// TODO: restore these to regular integers. Instead have one copy
		// of the counters per thread and collect them at convenient
		// synchronization points
#ifdef ATOMIC_LLONG_LOCK_FREE
		aux::array<std::atomic<std::int64_t>, num_counters> m_stats_counter;
#else
		// if the atomic type is't lock-free, use a single lock instead, for
		// the whole array
		mutable std::mutex m_mutex;
		aux::array<std::int64_t, num_counters> m_stats_counter;
#endif
	};
}

#endif
