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

#ifndef TORRENT_SETTINGS_PACK_HPP_INCLUDED
#define TORRENT_SETTINGS_PACK_HPP_INCLUDED

namespace libtorrent
{
	namespace aux { struct session_impl; }
	// #error add an API to query a settings_pack as well
	// #error maybe convert all bool types into int-types as well
	struct settings_pack
	{
		friend struct aux::session_impl;
		friend struct disk_io_thread;

		void set_str(int name, std::string val);
		void set_int(int name, int val);
		void set_bool(int name, bool val);
	
		enum type_bases
		{
			string_type_base = 0x10000000,
			int_type_base =    0x20000000,
			bool_type_base =   0x30000000,
			type_mask =        0xf0000000,
			index_mask =       0x0fffffff,
		};

		enum string_types
		{
			user_agent = string_type_base,
			announce_ip,
			mmap_cache,

			max_string_setting_internal,
			num_string_settings = max_string_setting_internal - string_type_base
		};

		enum bool_types
		{
			allow_multiple_connections_per_ip = bool_type_base,
			ignore_limits_on_local_network,
			send_redundant_have,
			lazy_bitfields,
#ifndef TRENT_DISABLE_DHT
			use_dht_as_fallback,
#else
			unused1,
#endif
			upnp_ignore_nonrouters,
			use_parole_mode,
			use_read_cache,
			dont_flush_write_cache,
			explicit_read_cache,
			coalesce_reads,
			coalesce_writes,
			auto_manage_prefer_seeds,
			dont_count_slow_torrents,
			close_redundant_connections,
			prioritize_partial_pieces,
			rate_limit_ip_overhead,
			announce_to_all_trackers,
			announce_to_all_tiers,
			prefer_udp_trackers,
			strict_super_seeding,
#ifndef TORRENT_DISABLE_MLOCK
			lock_disk_cache,
#else
			unused2,
#endif
			optimize_hashing_for_speed,
			disable_hash_checks,
			allow_reordered_disk_operations,
			allow_i2p_mixed,
			drop_skipped_requests,
			low_prio_disk,
			volatile_read_cache,
			guided_read_cache,
			no_atime_storage,
			incoming_starts_queued_torrents,
			report_true_downloaded,
			strict_end_game_mode,
			broadcast_lsd,
			enable_outgoing_utp,
			enable_incoming_utp,
			enable_outgoing_tcp,
			enable_incoming_tcp,
			ignore_resume_timestamps,
			no_recheck_incomplete_resume,
			anonymous_mode,
			report_web_seed_downloads,
			utp_dynamic_sock_buf,
			rate_limit_utp,
			announce_double_nat,
			seeding_outgoing_connections,
			no_connect_privileged_ports,
			smooth_connects,
			always_send_user_agent,
			apply_ip_filter_to_trackers,
			use_disk_read_ahead,
			lock_files,
			contiguous_recv_buffer,
			ban_web_seeds,

			max_bool_setting_internal,
			num_bool_settings = max_bool_setting_internal - bool_type_base
		};

		enum int_types
		{
			tracker_completion_timeout = int_type_base,
			tracker_receive_timeout,
			stop_tracker_timeout,
			tracker_maximum_response_length,
			piece_timeout,
			request_timeout,
			request_queue_time,
			max_allowed_in_request_queue,
			max_out_request_queue,
			whole_pieces_threshold,
			peer_timeout,
			urlseed_timeout,
			urlseed_pipeline_size,
			urlseed_wait_retry,
			file_pool_size,
			max_failcount,
			min_reconnect_time,
			peer_connect_timeout,
			connection_speed,
			inactivity_timeout,
			unchoke_interval,
			optimistic_unchoke_interval,
			num_want,
			initial_picker_threshold,
			allowed_fast_set_size,
			suggest_mode,
			max_queued_disk_bytes,
			handshake_timeout,
			send_buffer_low_watermark,
			send_buffer_watermark,
			send_buffer_watermark_factor,
			choking_algorithm,
			seed_choking_algorithm,
			cache_size,
			cache_buffer_chunk_size,
			cache_expiry,
			explicit_cache_interval,
			disk_io_write_mode,
			disk_io_read_mode,
			outgoing_port,
			num_outgoing_ports,
			peer_tos,
			active_downloads,
			active_seeds,
			active_dht_limit,
			active_tracker_limit,
			active_lsd_limit,
			active_limit,
			auto_manage_interval,
			seed_time_limit,
			peer_turnover_interval,
			auto_scrape_interval,
			auto_scrape_min_interval,
			max_peerlist_size,
			max_paused_peerlist_size,
			min_announce_interval,
			auto_manage_startup,
			seeding_piece_quota,
			max_sparse_regions,
			max_rejects,
			recv_socket_buffer_size,
			send_socket_buffer_size,
			file_checks_delay_per_block,
			disk_cache_algorithm,
			read_cache_line_size,
			write_cache_line_size,
			optimistic_disk_retry,
			max_suggest_pieces,
			local_service_announce_interval,
			dht_announce_interval,
			udp_tracker_token_expiry,
			default_cache_min_age,
			num_optimistic_unchoke_slots,
			default_est_reciprocation_rate,
			increase_est_reciprocation_rate,
			decrease_est_reciprocation_rate,
			max_pex_peers,
			tick_interval,
			share_mode_target,
			upload_rate_limit,
			download_rate_limit,
			local_upload_rate_limit,
			local_download_rate_limit,
			dht_upload_rate_limit,
			unchoke_slots_limit,
			half_open_limit,
			connections_limit,
			utp_target_delay,
			utp_gain_factor,
			utp_min_timeout,
			utp_syn_resends,
			utp_fin_resends,
			utp_num_resends,
			utp_connect_timeout,
			utp_delayed_ack,
			utp_loss_multiplier,
			mixed_mode_algorithm,
			listen_queue_size,
			torrent_connect_boost,
			alert_queue_size,
			max_metadata_size,
			read_job_every,
			hashing_threads,
			checking_mem_usage,
			predictive_piece_announce,
			aio_threads,
			aio_max,
			network_threads,
			ssl_listen,
			tracker_backoff,
			share_ratio_limit,
			seed_time_ratio_limit,
			peer_turnover,
			peer_turnover_cutoff,

			max_int_setting_internal,
			num_int_settings = max_int_setting_internal - int_type_base
		};

		enum { no_piece_suggestions = 0, suggest_read_cache = 1 };

		enum choking_algorithm_t
		{
			fixed_slots_choker,
			auto_expand_choker,
			rate_based_choker,
			bittyrant_choker
		};

		enum seed_choking_algorithm_t
		{
			round_robin,
			fastest_upload,
			anti_leech
		};
 
		enum io_buffer_mode_t
		{
			enable_os_cache = 0,
			disable_os_cache_for_aligned_files = 1,
			disable_os_cache = 2
		};

		enum disk_cache_algo_t
		{ lru, largest_contiguous, avoid_readback };

		enum bandwidth_mixed_algo_t
		{
			// disables the mixed mode bandwidth balancing
			prefer_tcp = 0,

			// does not throttle uTP, throttles TCP to the same proportion
			// of throughput as there are TCP connections
			peer_proportional = 1
		};

	private:

		std::vector<std::pair<int, std::string> > m_strings;
		std::vector<std::pair<int, int> > m_ints;
		std::vector<std::pair<int, bool> > m_bools;
	};
}

#endif

