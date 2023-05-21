/*

Copyright (c) 2014-2022, Arvid Norberg
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2017, Steven Siloti
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

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/session_settings.hpp"

#include <algorithm>

namespace {

	template <class T>
	bool compare_first(std::pair<std::uint16_t, T> const& lhs
		, std::pair<std::uint16_t, T> const& rhs)
	{
		return lhs.first < rhs.first;
	}

	template <class T>
	void insort_replace(std::vector<std::pair<std::uint16_t, T>>& c, std::pair<std::uint16_t, T> v)
	{
		auto i = std::lower_bound(c.begin(), c.end(), v, &compare_first<T>);
		if (i != c.end() && i->first == v.first) i->second = std::move(v.second);
		else c.emplace(i, std::move(v));
	}

	// return the string, unless it's null, in which case the empty string is
	// returned
	char const* ensure_string(char const* str)
	{ return str == nullptr ? "" : str; }
}

namespace libtorrent {

	struct str_setting_entry_t
	{
		// the name of this setting. used for serialization and deserialization
		char const* name;
		// if present, this function is called when the setting is changed
		void (aux::session_impl::*fun)();
		char const *default_value;
	};

	struct int_setting_entry_t
	{
		// the name of this setting. used for serialization and deserialization
		char const* name;
		// if present, this function is called when the setting is changed
		void (aux::session_impl::*fun)();
		int default_value;
	};

	struct bool_setting_entry_t
	{
		// the name of this setting. used for serialization and deserialization
		char const* name;
		// if present, this function is called when the setting is changed
		void (aux::session_impl::*fun)();
		bool default_value;
	};


#define SET(name, default_value, fun) { #name, fun, default_value }

#if TORRENT_ABI_VERSION == 1
#define DEPRECATED_SET(name, default_value, fun) { #name, fun, default_value }
#define DEPRECATED_SET_STR(name, default_value, fun) { #name, fun, default_value }
#define DEPRECATED2_SET(name, default_value, fun) { #name, fun, default_value }
#elif TORRENT_ABI_VERSION == 2
#define DEPRECATED_SET(name, default_value, fun) { "", nullptr, 0 }
#define DEPRECATED_SET_STR(name, default_value, fun) { "", nullptr, nullptr }
#define DEPRECATED2_SET(name, default_value, fun) { #name, fun, default_value }
#else
#define DEPRECATED_SET(name, default_value, fun) { "", nullptr, 0 }
#define DEPRECATED_SET_STR(name, default_value, fun) { "", nullptr, nullptr }
#define DEPRECATED2_SET(name, default_value, fun) { "", nullptr, 0 }
#endif

#ifdef TORRENT_WINDOWS
constexpr int CLOSE_FILE_INTERVAL = 240;
#else
constexpr int CLOSE_FILE_INTERVAL = 0;
#endif

#ifdef TORRENT_WINDOWS
constexpr int DISK_WRITE_MODE = settings_pack::write_through;
#else
constexpr int DISK_WRITE_MODE = settings_pack::enable_os_cache;
#endif

		// tested to fail with _MSC_VER <= 1916. The actual version condition
#if !defined _MSC_VER
#define CONSTEXPR_SETTINGS constexpr
#else
#define CONSTEXPR_SETTINGS
#endif

	namespace {

	using aux::session_impl;

	CONSTEXPR_SETTINGS
	aux::array<str_setting_entry_t, settings_pack::num_string_settings> const str_settings
	({{
		SET(user_agent, "libtorrent/" LIBTORRENT_VERSION, &session_impl::update_user_agent),
		SET(announce_ip, nullptr, nullptr),
		DEPRECATED_SET_STR(mmap_cache, nullptr, nullptr),
		SET(handshake_client_version, nullptr, nullptr),
		SET(outgoing_interfaces, "", &session_impl::update_outgoing_interfaces),
		SET(listen_interfaces, "0.0.0.0:6881,[::]:6881", &session_impl::update_listen_interfaces),
		SET(proxy_hostname, "", &session_impl::update_proxy),
		SET(proxy_username, "", &session_impl::update_proxy),
		SET(proxy_password, "", &session_impl::update_proxy),
		SET(i2p_hostname, "", &session_impl::update_i2p_bridge),
		SET(peer_fingerprint, "-LT2090-", nullptr),
		SET(dht_bootstrap_nodes, "dht.libtorrent.org:25401", &session_impl::update_dht_bootstrap_nodes)
	}});

	CONSTEXPR_SETTINGS
	aux::array<bool_setting_entry_t, settings_pack::num_bool_settings> const bool_settings
	({{
		SET(allow_multiple_connections_per_ip, false, nullptr),
		DEPRECATED_SET(ignore_limits_on_local_network, true, &session_impl::update_ignore_rate_limits_on_local_network),
		SET(send_redundant_have, true, nullptr),
		DEPRECATED_SET(lazy_bitfields, false, nullptr),
		SET(use_dht_as_fallback, false, nullptr),
		SET(upnp_ignore_nonrouters, false, nullptr),
		SET(use_parole_mode, true, nullptr),
		DEPRECATED_SET(use_read_cache, true, nullptr),
		DEPRECATED_SET(use_write_cache, true, nullptr),
		DEPRECATED_SET(dont_flush_write_cache, false, nullptr),
		DEPRECATED_SET(coalesce_reads, false, nullptr),
		DEPRECATED_SET(coalesce_writes, false, nullptr),
		SET(auto_manage_prefer_seeds, false, nullptr),
		SET(dont_count_slow_torrents, true, &session_impl::update_count_slow),
		SET(close_redundant_connections, true, nullptr),
		SET(prioritize_partial_pieces, false, nullptr),
		SET(rate_limit_ip_overhead, true, nullptr),
		SET(announce_to_all_trackers, false, nullptr),
		SET(announce_to_all_tiers, false, nullptr),
		SET(prefer_udp_trackers, true, nullptr),
		DEPRECATED_SET(strict_super_seeding, false, nullptr),
		DEPRECATED_SET(lock_disk_cache, false, nullptr),
		SET(disable_hash_checks, false, nullptr),
		SET(allow_i2p_mixed, false, nullptr),
		DEPRECATED_SET(low_prio_disk, true, nullptr),
		DEPRECATED2_SET(volatile_read_cache, false, nullptr),
		DEPRECATED_SET(guided_read_cache, false, nullptr),
		SET(no_atime_storage, true, nullptr),
		SET(incoming_starts_queued_torrents, false, nullptr),
		SET(report_true_downloaded, false, nullptr),
		SET(strict_end_game_mode, true, nullptr),
		DEPRECATED_SET(broadcast_lsd, true, nullptr),
		SET(enable_outgoing_utp, true, nullptr),
		SET(enable_incoming_utp, true, nullptr),
		SET(enable_outgoing_tcp, true, nullptr),
		SET(enable_incoming_tcp, true, nullptr),
		DEPRECATED_SET(ignore_resume_timestamps, false, nullptr),
		SET(no_recheck_incomplete_resume, false, nullptr),
		SET(anonymous_mode, false, nullptr),
		SET(report_web_seed_downloads, true, &session_impl::update_report_web_seed_downloads),
		DEPRECATED_SET(rate_limit_utp, true, &session_impl::update_rate_limit_utp),
		DEPRECATED_SET(announce_double_nat, false, nullptr),
		SET(seeding_outgoing_connections, true, nullptr),
		SET(no_connect_privileged_ports, false, &session_impl::update_privileged_ports),
		SET(smooth_connects, true, nullptr),
		SET(always_send_user_agent, false, nullptr),
		SET(apply_ip_filter_to_trackers, true, nullptr),
		DEPRECATED_SET(use_disk_read_ahead, true, nullptr),
		DEPRECATED_SET(lock_files, false, nullptr),
		DEPRECATED_SET(contiguous_recv_buffer, true, nullptr),
		SET(ban_web_seeds, true, nullptr),
		DEPRECATED2_SET(allow_partial_disk_writes, true, nullptr),
		DEPRECATED_SET(force_proxy, false, nullptr),
		SET(support_share_mode, true, nullptr),
		DEPRECATED2_SET(support_merkle_torrents, false, nullptr),
		SET(report_redundant_bytes, true, nullptr),
		SET(listen_system_port_fallback, true, nullptr),
		DEPRECATED_SET(use_disk_cache_pool, false, nullptr),
		SET(announce_crypto_support, true, nullptr),
		SET(enable_upnp, true, &session_impl::update_upnp),
		SET(enable_natpmp, true, &session_impl::update_natpmp),
		SET(enable_lsd, true, &session_impl::update_lsd),
		SET(enable_dht, true, &session_impl::update_dht),
		SET(prefer_rc4, false, nullptr),
		SET(proxy_hostnames, true, nullptr),
		SET(proxy_peer_connections, true, nullptr),
		SET(auto_sequential, true, &session_impl::update_auto_sequential),
		SET(proxy_tracker_connections, true, nullptr),
		SET(enable_ip_notifier, true, &session_impl::update_ip_notifier),
		SET(dht_prefer_verified_node_ids, true, nullptr),
		SET(dht_restrict_routing_ips, true, nullptr),
		SET(dht_restrict_search_ips, true, nullptr),
		SET(dht_extended_routing_table, true, nullptr),
		SET(dht_aggressive_lookups, true, nullptr),
		SET(dht_privacy_lookups, false, nullptr),
		SET(dht_enforce_node_id, false, nullptr),
		SET(dht_ignore_dark_internet, true, nullptr),
		SET(dht_read_only, false, nullptr),
		SET(piece_extent_affinity, false, nullptr),
		SET(validate_https_trackers, true, &session_impl::update_validate_https),
		SET(ssrf_mitigation, true, nullptr),
		SET(allow_idna, false, nullptr),
		SET(enable_set_file_valid_data, false, nullptr),
		SET(socks5_udp_send_local_ep, false, nullptr),
	}});

	CONSTEXPR_SETTINGS
	aux::array<int_setting_entry_t, settings_pack::num_int_settings> const int_settings
	({{
		SET(tracker_completion_timeout, 30, nullptr),
		SET(tracker_receive_timeout, 10, nullptr),
		SET(stop_tracker_timeout, 5, nullptr),
		SET(tracker_maximum_response_length, 1024*1024, nullptr),
		SET(piece_timeout, 20, nullptr),
		SET(request_timeout, 60, nullptr),
		SET(request_queue_time, 3, nullptr),
		SET(max_allowed_in_request_queue, 2000, nullptr),
		SET(max_out_request_queue, 500, nullptr),
		SET(whole_pieces_threshold, 20, nullptr),
		SET(peer_timeout, 120, nullptr),
		SET(urlseed_timeout, 20, nullptr),
		SET(urlseed_pipeline_size, 5, nullptr),
		SET(urlseed_wait_retry, 30, nullptr),
		SET(file_pool_size, 40, nullptr),
		SET(max_failcount, 3, &session_impl::update_max_failcount),
		SET(min_reconnect_time, 60, nullptr),
		SET(peer_connect_timeout, 15, nullptr),
		SET(connection_speed, 30, &session_impl::update_connection_speed),
		SET(inactivity_timeout, 600, nullptr),
		SET(unchoke_interval, 15, nullptr),
		SET(optimistic_unchoke_interval, 30, nullptr),
		SET(num_want, 200, nullptr),
		SET(initial_picker_threshold, 4, nullptr),
		SET(allowed_fast_set_size, 5, nullptr),
		SET(suggest_mode, settings_pack::no_piece_suggestions, nullptr),
		SET(max_queued_disk_bytes, 1024 * 1024, nullptr),
		SET(handshake_timeout, 10, nullptr),
		SET(send_buffer_low_watermark, 10 * 1024, nullptr),
		SET(send_buffer_watermark, 500 * 1024, nullptr),
		SET(send_buffer_watermark_factor, 50, nullptr),
		SET(choking_algorithm, settings_pack::fixed_slots_choker, nullptr),
		SET(seed_choking_algorithm, settings_pack::round_robin, nullptr),
		DEPRECATED_SET(cache_size, 2048, nullptr),
		DEPRECATED_SET(cache_buffer_chunk_size, 0, nullptr),
		DEPRECATED_SET(cache_expiry, 300, nullptr),
		SET(disk_io_write_mode, DISK_WRITE_MODE, nullptr),
		SET(disk_io_read_mode, settings_pack::enable_os_cache, nullptr),
		SET(outgoing_port, 0, nullptr),
		SET(num_outgoing_ports, 0, nullptr),
		SET(peer_dscp, 0x04, &session_impl::update_peer_dscp),
		SET(active_downloads, 3, &session_impl::trigger_auto_manage),
		SET(active_seeds, 5, &session_impl::trigger_auto_manage),
		SET(active_checking, 1, &session_impl::trigger_auto_manage),
		SET(active_dht_limit, 88, nullptr),
		SET(active_tracker_limit, 1600, nullptr),
		SET(active_lsd_limit, 60, nullptr),
		SET(active_limit, 500, &session_impl::trigger_auto_manage),
		DEPRECATED_SET(active_loaded_limit, 0, &session_impl::trigger_auto_manage),
		SET(auto_manage_interval, 30, nullptr),
		SET(seed_time_limit, 24 * 60 * 60, nullptr),
		SET(auto_scrape_interval, 1800, nullptr),
		SET(auto_scrape_min_interval, 300, nullptr),
		SET(max_peerlist_size, 3000, nullptr),
		SET(max_paused_peerlist_size, 1000, nullptr),
		SET(min_announce_interval, 5 * 60, nullptr),
		SET(auto_manage_startup, 60, nullptr),
		SET(seeding_piece_quota, 20, nullptr),
		// TODO: deprecate this
		SET(max_rejects, 50, nullptr),
		SET(recv_socket_buffer_size, 0, &session_impl::update_socket_buffer_size),
		SET(send_socket_buffer_size, 0, &session_impl::update_socket_buffer_size),
		SET(max_peer_recv_buffer_size, 2 * 1024 * 1024, nullptr),
		DEPRECATED_SET(file_checks_delay_per_block, 0, nullptr),
		DEPRECATED2_SET(read_cache_line_size, 32, nullptr),
		DEPRECATED2_SET(write_cache_line_size, 16, nullptr),
		SET(optimistic_disk_retry, 10 * 60, nullptr),
		SET(max_suggest_pieces, 16, nullptr),
		SET(local_service_announce_interval, 5 * 60, nullptr),
		SET(dht_announce_interval, 15 * 60, &session_impl::update_dht_announce_interval),
		SET(udp_tracker_token_expiry, 60, nullptr),
		DEPRECATED_SET(default_cache_min_age, 1, nullptr),
		SET(num_optimistic_unchoke_slots, 0, nullptr),
		DEPRECATED_SET(default_est_reciprocation_rate, 16000, nullptr),
		DEPRECATED_SET(increase_est_reciprocation_rate, 20, nullptr),
		DEPRECATED_SET(decrease_est_reciprocation_rate, 3, nullptr),
		SET(max_pex_peers, 50, nullptr),
		SET(tick_interval, 500, nullptr),
		SET(share_mode_target, 3, nullptr),
		SET(upload_rate_limit, 0, &session_impl::update_upload_rate),
		SET(download_rate_limit, 0, &session_impl::update_download_rate),
		DEPRECATED_SET(local_upload_rate_limit, 0, &session_impl::update_local_upload_rate),
		DEPRECATED_SET(local_download_rate_limit, 0, &session_impl::update_local_download_rate),
		SET(dht_upload_rate_limit, 8000, &session_impl::update_dht_upload_rate_limit),
		SET(unchoke_slots_limit, 8, &session_impl::update_unchoke_limit),
		DEPRECATED_SET(half_open_limit, 0, nullptr),
		SET(connections_limit, 200, &session_impl::update_connections_limit),
		SET(connections_slack, 10, nullptr),
		SET(utp_target_delay, 100, nullptr),
		SET(utp_gain_factor, 3000, nullptr),
		SET(utp_min_timeout, 500, nullptr),
		SET(utp_syn_resends, 2, nullptr),
		SET(utp_fin_resends, 2, nullptr),
		SET(utp_num_resends, 3, nullptr),
		SET(utp_connect_timeout, 3000, nullptr),
		DEPRECATED_SET(utp_delayed_ack, 0, nullptr),
		SET(utp_loss_multiplier, 50, nullptr),
		SET(mixed_mode_algorithm, settings_pack::peer_proportional, nullptr),
		SET(listen_queue_size, 5, nullptr),
		SET(torrent_connect_boost, 30, nullptr),
		SET(alert_queue_size, 2000, &session_impl::update_alert_queue_size),
		SET(max_metadata_size, 3 * 1024 * 10240, nullptr),
		SET(hashing_threads, 1, &session_impl::update_disk_threads),
		SET(checking_mem_usage, 256, nullptr),
		SET(predictive_piece_announce, 0, nullptr),
		SET(aio_threads, 10, &session_impl::update_disk_threads),
		DEPRECATED_SET(aio_max, 300, nullptr),
		DEPRECATED_SET(network_threads, 0, nullptr),
		DEPRECATED_SET(ssl_listen, 0, &session_impl::update_ssl_listen),
		SET(tracker_backoff, 250, nullptr),
		SET(share_ratio_limit, 200, nullptr),
		SET(seed_time_ratio_limit, 700, nullptr),
		SET(peer_turnover, 4, nullptr),
		SET(peer_turnover_cutoff, 90, nullptr),
		SET(peer_turnover_interval, 300, nullptr),
		SET(connect_seed_every_n_download, 10, nullptr),
		SET(max_http_recv_buffer_size, 4*1024*204, nullptr),
		SET(max_retry_port_bind, 10, nullptr),
		SET(alert_mask, int(static_cast<std::uint32_t>(alert_category::error)), &session_impl::update_alert_mask),
		SET(out_enc_policy, settings_pack::pe_enabled, nullptr),
		SET(in_enc_policy, settings_pack::pe_enabled, nullptr),
		SET(allowed_enc_level, settings_pack::pe_both, nullptr),
		SET(inactive_down_rate, 2048, nullptr),
		SET(inactive_up_rate, 2048, nullptr),
		SET(proxy_type, settings_pack::none, &session_impl::update_proxy),
		SET(proxy_port, 0, &session_impl::update_proxy),
		SET(i2p_port, 0, &session_impl::update_i2p_bridge),
		DEPRECATED_SET(cache_size_volatile, 256, nullptr),
		SET(urlseed_max_request_bytes, 16 * 1024 * 1024, nullptr),
		SET(web_seed_name_lookup_retry, 1800, nullptr),
		SET(close_file_interval, CLOSE_FILE_INTERVAL, nullptr),
		SET(utp_cwnd_reduce_timer, 100, nullptr),
		SET(max_web_seed_connections, 3, nullptr),
		SET(resolver_cache_timeout, 1200, &session_impl::update_resolver_cache_timeout),
		SET(send_not_sent_low_watermark, 16384, nullptr),
		SET(rate_choker_initial_threshold, 1024, nullptr),
		SET(upnp_lease_duration, 3600, nullptr),
		SET(max_concurrent_http_announces, 50, nullptr),
		SET(dht_max_peers_reply, 100, nullptr),
		SET(dht_search_branching, 5, nullptr),
		SET(dht_max_fail_count, 20, nullptr),
		SET(dht_max_torrents, 2000, nullptr),
		SET(dht_max_dht_items, 700, nullptr),
		SET(dht_max_peers, 500, nullptr),
		SET(dht_max_torrent_search_reply, 20, nullptr),
		SET(dht_block_timeout, 5 * 60, nullptr),
		SET(dht_block_ratelimit, 5, nullptr),
		SET(dht_item_lifetime, 0, nullptr),
		SET(dht_sample_infohashes_interval, 21600, nullptr),
		SET(dht_max_infohashes_sample_count, 20, nullptr),
		SET(max_piece_count, 0x200000, nullptr),
		SET(metadata_token_limit, 2500000, nullptr),
		SET(disk_write_mode, settings_pack::mmap_write_mode_t::auto_mmap_write, nullptr),
		SET(mmap_file_size_cutoff, 40, nullptr),
		SET(i2p_inbound_quantity, 3, nullptr),
		SET(i2p_outbound_quantity, 3, nullptr),
		SET(i2p_inbound_length, 3, nullptr),
		SET(i2p_outbound_length, 3, nullptr)
	}});

#undef SET
#undef DEPRECATED_SET
#undef CONSTEXPR_SETTINGS

	} // anonymous namespace

	int setting_by_name(string_view const key)
	{
		for (int k = 0; k < str_settings.end_index(); ++k)
		{
			if (key != str_settings[k].name) continue;
			return settings_pack::string_type_base + k;
		}
		for (int k = 0; k < int_settings.end_index(); ++k)
		{
			if (key != int_settings[k].name) continue;
			return settings_pack::int_type_base + k;
		}
		for (int k = 0; k < bool_settings.end_index(); ++k)
		{
			if (key != bool_settings[k].name) continue;
			return settings_pack::bool_type_base + k;
		}

		// backwards compatibility with previous name
		if (key == "peer_tos")
			return settings_pack::peer_dscp;

		return -1;
	}

	char const* name_for_setting(int s)
	{
		switch (s & settings_pack::type_mask)
		{
			case settings_pack::string_type_base:
				return str_settings[s - settings_pack::string_type_base].name;
			case settings_pack::int_type_base:
				return int_settings[s - settings_pack::int_type_base].name;
			case settings_pack::bool_type_base:
				return bool_settings[s - settings_pack::bool_type_base].name;
		}
		return "";
	}

	settings_pack load_pack_from_dict(bdecode_node const& settings)
	{
		settings_pack pack;

		for (int i = 0; i < settings.dict_size(); ++i)
		{
			string_view key;
			bdecode_node val;
			std::tie(key, val) = settings.dict_at(i);
			switch (val.type())
			{
				case bdecode_node::dict_t:
				case bdecode_node::list_t:
					continue;
				case bdecode_node::int_t:
				{
					bool found = false;
					for (int k = 0; k < int_settings.end_index(); ++k)
					{
						if (key != int_settings[k].name) continue;
						pack.set_int(settings_pack::int_type_base | k, int(val.int_value()));
						found = true;
						break;
					}
					if (found) continue;
					for (int k = 0; k < bool_settings.end_index(); ++k)
					{
						if (key != bool_settings[k].name) continue;
						pack.set_bool(settings_pack::bool_type_base | k, val.int_value() != 0);
						break;
					}
				}
				break;
			case bdecode_node::string_t:
				for (int k = 0; k < str_settings.end_index(); ++k)
				{
					if (key != str_settings[k].name) continue;
					pack.set_str(settings_pack::string_type_base + k, val.string_value().to_string());
					break;
				}
				break;
			case bdecode_node::none_t:
				break;
			}
		}
		return pack;
	}

	settings_pack non_default_settings(aux::session_settings const& sett)
	{
		settings_pack ret;
		sett.bulk_get([&ret](aux::session_settings_single_thread const& s)
		{
		// loop over all settings that differ from default
			for (std::uint16_t i = 0; i < settings_pack::num_string_settings; ++i)
			{
				std::uint16_t const n = i | settings_pack::string_type_base;
				if (ensure_string(str_settings[i].default_value) == s.get_str(n)) continue;
				ret.set_str(n, s.get_str(n));
			}

			for (std::uint16_t i = 0; i < settings_pack::num_int_settings; ++i)
			{
				std::uint16_t const n = i | settings_pack::int_type_base;
				if (int_settings[i].default_value == s.get_int(n)) continue;
				ret.set_int(n, s.get_int(n));
			}

			for (std::uint16_t i = 0; i < settings_pack::num_bool_settings; ++i)
			{
				std::uint16_t const n = i | settings_pack::bool_type_base;
				if (bool_settings[i].default_value == s.get_bool(n)) continue;
				ret.set_bool(n, s.get_bool(n));
			}
		});
		return ret;
	}

	void save_settings_to_dict(settings_pack const& sett, entry::dictionary_type& out)
	{
		struct visitor
		{
			void operator()(std::uint16_t i, std::string const& str) { out[str_settings[i & settings_pack::index_mask].name] = str; }
			void operator()(std::uint16_t i, int val) { out[int_settings[i & settings_pack::index_mask].name] = val; }
			void operator()(std::uint16_t i, bool val) { out[bool_settings[i & settings_pack::index_mask].name] = val; }
			entry::dictionary_type& out;
		};
		sett.for_each(visitor{out});
	}

	void run_all_updates(aux::session_impl& ses)
	{
		using fun_t = void (aux::session_impl::*)();
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			fun_t const& f = str_settings[i].fun;
			if (f) (ses.*f)();
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			fun_t const& f = int_settings[i].fun;
			if (f) (ses.*f)();
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			fun_t const& f = bool_settings[i].fun;
			if (f) (ses.*f)();
		}
	}

	void initialize_default_settings(aux::session_settings_single_thread& s)
	{
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			if (str_settings[i].default_value == nullptr) continue;
			s.set_str(settings_pack::string_type_base | i, str_settings[i].default_value);
			TORRENT_ASSERT(s.get_str(settings_pack::string_type_base + i) == str_settings[i].default_value);
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			s.set_int(settings_pack::int_type_base | i, int_settings[i].default_value);
			TORRENT_ASSERT(s.get_int(settings_pack::int_type_base + i) == int_settings[i].default_value);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			s.set_bool(settings_pack::bool_type_base | i, bool_settings[i].default_value);
			TORRENT_ASSERT(s.get_bool(settings_pack::bool_type_base + i) == bool_settings[i].default_value);
		}
	}

	settings_pack default_settings()
	{
		settings_pack ret;
		// TODO: it would be nice to reserve() these vectors up front
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			if (str_settings[i].default_value == nullptr) continue;
			ret.set_str(settings_pack::string_type_base + i, str_settings[i].default_value);
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			ret.set_int(settings_pack::int_type_base + i, int_settings[i].default_value);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			ret.set_bool(settings_pack::bool_type_base + i, bool_settings[i].default_value);
		}
		return ret;
	}

	void apply_pack(settings_pack const* pack, aux::session_settings& sett
		, aux::session_impl* ses)
	{
		using fun_t = void (aux::session_impl::*)();
		std::vector<fun_t> callbacks;

		sett.bulk_set([&](aux::session_settings_single_thread& s)
		{
			apply_pack_impl(pack, s, ses ? &callbacks : nullptr);
		});

		// call the callbacks once all the settings have been applied, and
		// only once per callback
		for (auto const& f : callbacks)
		{
			(ses->*f)();
		}
	}

	void apply_pack_impl(settings_pack const* pack, aux::session_settings_single_thread& sett
		, std::vector<void(aux::session_impl::*)()>* callbacks)
	{
		for (auto const& p : pack->m_strings)
		{
			// disregard setting indices that are not string types
			if ((p.first & settings_pack::type_mask) != settings_pack::string_type_base)
				continue;

			// ignore settings that are out of bounds
			int const index = p.first & settings_pack::index_mask;
			TORRENT_ASSERT_PRECOND(index >= 0 && index < settings_pack::num_string_settings);
			if (index < 0 || index >= settings_pack::num_string_settings)
				continue;

			// if the value did not change, don't call the update callback
			if (sett.get_str(p.first) == p.second) continue;

			sett.set_str(p.first, p.second);
			str_setting_entry_t const& sa = str_settings[index];

			if (sa.fun && callbacks
				&& std::find(callbacks->begin(), callbacks->end(), sa.fun) == callbacks->end())
				callbacks->push_back(sa.fun);
		}

		for (auto const& p : pack->m_ints)
		{
			// disregard setting indices that are not int types
			if ((p.first & settings_pack::type_mask) != settings_pack::int_type_base)
				continue;

			// ignore settings that are out of bounds
			int const index = p.first & settings_pack::index_mask;
			TORRENT_ASSERT_PRECOND(index >= 0 && index < settings_pack::num_int_settings);
			if (index < 0 || index >= settings_pack::num_int_settings)
				continue;

			// if the value did not change, don't call the update callback
			if (sett.get_int(p.first) == p.second) continue;

			sett.set_int(p.first, p.second);
			int_setting_entry_t const& sa = int_settings[index];
			if (sa.fun && callbacks
				&& std::find(callbacks->begin(), callbacks->end(), sa.fun) == callbacks->end())
				callbacks->push_back(sa.fun);
		}

		for (auto const& p : pack->m_bools)
		{
			// disregard setting indices that are not bool types
			if ((p.first & settings_pack::type_mask) != settings_pack::bool_type_base)
				continue;

			// ignore settings that are out of bounds
			int const index = p.first & settings_pack::index_mask;
			TORRENT_ASSERT_PRECOND(index >= 0 && index < settings_pack::num_bool_settings);
			if (index < 0 || index >= settings_pack::num_bool_settings)
				continue;

			// if the value did not change, don't call the update callback
			if (sett.get_bool(p.first) == p.second) continue;

			sett.set_bool(p.first, p.second);
			bool_setting_entry_t const& sa = bool_settings[index];
			if (sa.fun && callbacks
				&& std::find(callbacks->begin(), callbacks->end(), sa.fun) == callbacks->end())
				callbacks->push_back(sa.fun);
		}
	}

	void settings_pack::set_str(int const name, std::string val)
	{
		TORRENT_ASSERT((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return;
		std::pair<std::uint16_t, std::string> v(aux::numeric_cast<std::uint16_t>(name), std::move(val));
		insort_replace(m_strings, std::move(v));
	}

	void settings_pack::set_int(int const name, int const val)
	{
		TORRENT_ASSERT((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return;
		std::pair<std::uint16_t, int> v(aux::numeric_cast<std::uint16_t>(name), val);
		insort_replace(m_ints, v);
	}

	void settings_pack::set_bool(int const name, bool const val)
	{
		TORRENT_ASSERT((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return;
		std::pair<std::uint16_t, bool> v(aux::numeric_cast<std::uint16_t>(name), val);
		insort_replace(m_bools, v);
	}

	bool settings_pack::has_val(int const name) const
	{
		switch (name & type_mask)
		{
			case string_type_base:
			{
				// this is an optimization. If the settings pack is complete,
				// i.e. has every key, we don't need to search, it's just a lookup
				if (m_strings.size() == settings_pack::num_string_settings)
					return true;
				std::pair<std::uint16_t, std::string> v(aux::numeric_cast<std::uint16_t>(name), std::string());
				auto i = std::lower_bound(m_strings.begin(), m_strings.end(), v
						, &compare_first<std::string>);
				return i != m_strings.end() && i->first == name;
			}
			case int_type_base:
			{
				// this is an optimization. If the settings pack is complete,
				// i.e. has every key, we don't need to search, it's just a lookup
				if (m_ints.size() == settings_pack::num_int_settings)
					return true;
				std::pair<std::uint16_t, int> v(aux::numeric_cast<std::uint16_t>(name), 0);
				auto i = std::lower_bound(m_ints.begin(), m_ints.end(), v
						, &compare_first<int>);
				return i != m_ints.end() && i->first == name;
			}
			case bool_type_base:
			{
				// this is an optimization. If the settings pack is complete,
				// i.e. has every key, we don't need to search, it's just a lookup
				if (m_bools.size() == settings_pack::num_bool_settings)
					return true;
				std::pair<std::uint16_t, bool> v(aux::numeric_cast<std::uint16_t>(name), false);
				auto i = std::lower_bound(m_bools.begin(), m_bools.end(), v
						, &compare_first<bool>);
				return i != m_bools.end() && i->first == name;
			}
		}
		TORRENT_ASSERT_FAIL();
		return false;
	}

	std::string const& settings_pack::get_str(int name) const
	{
		static std::string const empty;
		TORRENT_ASSERT_PRECOND((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return empty;

		// this is an optimization. If the settings pack is complete,
		// i.e. has every key, we don't need to search, it's just a lookup
		if (m_strings.size() == settings_pack::num_string_settings)
		{
			TORRENT_ASSERT(m_strings[name & index_mask].first == name);
			return m_strings[name & index_mask].second;
		}
		std::pair<std::uint16_t, std::string> v(aux::numeric_cast<std::uint16_t>(name), std::string());
		auto i = std::lower_bound(m_strings.begin(), m_strings.end(), v
				, &compare_first<std::string>);
		if (i != m_strings.end() && i->first == name) return i->second;

		if (str_settings[name & index_mask].default_value == nullptr)
			return empty;

		static std::string tmp;
		tmp = str_settings[name & index_mask].default_value;
		return tmp;
	}

	int settings_pack::get_int(int name) const
	{
		TORRENT_ASSERT_PRECOND((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return 0;

		// this is an optimization. If the settings pack is complete,
		// i.e. has every key, we don't need to search, it's just a lookup
		if (m_ints.size() == settings_pack::num_int_settings)
		{
			TORRENT_ASSERT(m_ints[name & index_mask].first == name);
			return m_ints[name & index_mask].second;
		}
		std::pair<std::uint16_t, int> v(aux::numeric_cast<std::uint16_t>(name), 0);
		auto i = std::lower_bound(m_ints.begin(), m_ints.end(), v
				, &compare_first<int>);
		if (i != m_ints.end() && i->first == name) return i->second;

		return int_settings[name & index_mask].default_value;
	}

	bool settings_pack::get_bool(int name) const
	{
		TORRENT_ASSERT_PRECOND((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return false;

		// this is an optimization. If the settings pack is complete,
		// i.e. has every key, we don't need to search, it's just a lookup
		if (m_bools.size() == settings_pack::num_bool_settings)
		{
			TORRENT_ASSERT(m_bools[name & index_mask].first == name);
			return m_bools[name & index_mask].second;
		}
		std::pair<std::uint16_t, bool> v(aux::numeric_cast<std::uint16_t>(name), false);
		auto i = std::lower_bound(m_bools.begin(), m_bools.end(), v
			, &compare_first<bool>);
		if (i != m_bools.end() && i->first == name) return i->second;

		return bool_settings[name & index_mask].default_value;
	}

	void settings_pack::clear()
	{
		m_strings.clear();
		m_ints.clear();
		m_bools.clear();
	}

	void settings_pack::clear(int const name)
	{
		switch (name & type_mask)
		{
			case string_type_base:
			{
				std::pair<std::uint16_t, std::string> v(aux::numeric_cast<std::uint16_t>(name), std::string());
				auto const i = std::lower_bound(m_strings.begin(), m_strings.end()
					, v, &compare_first<std::string>);
				if (i != m_strings.end() && i->first == name) m_strings.erase(i);
				break;
			}
			case int_type_base:
			{
				std::pair<std::uint16_t, int> v(aux::numeric_cast<std::uint16_t>(name), 0);
				auto const i = std::lower_bound(m_ints.begin(), m_ints.end()
					, v, &compare_first<int>);
				if (i != m_ints.end() && i->first == name) m_ints.erase(i);
				break;
			}
			case bool_type_base:
			{
				std::pair<std::uint16_t, bool> v(aux::numeric_cast<std::uint16_t>(name), false);
				auto const i = std::lower_bound(m_bools.begin(), m_bools.end()
					, v, &compare_first<bool>);
				if (i != m_bools.end() && i->first == name) m_bools.erase(i);
				break;
			}
		}
	}
}
