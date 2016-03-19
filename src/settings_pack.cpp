/*

Copyright (c) 2012-2016, Arvid Norberg
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
#include "libtorrent/session_settings.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include <algorithm>

namespace {

	template <class T>
	bool compare_first(std::pair<boost::uint16_t, T> const& lhs
		, std::pair<boost::uint16_t, T> const& rhs)
	{
		return lhs.first < rhs.first;
	}

	template <class T>
	void insort_replace(std::vector<std::pair<boost::uint16_t, T> >& c, std::pair<boost::uint16_t, T> const& v)
	{
		typedef std::vector<std::pair<boost::uint16_t, T> > container_t;
		typename container_t::iterator i = std::lower_bound(c.begin(), c.end(), v
			, &compare_first<T>);
		if (i != c.end() && i->first == v.first) i->second = v.second;
		else c.insert(i, v);
	}
}

namespace libtorrent
{
	struct str_setting_entry_t
	{
		// the name of this setting. used for serialization and deserialization
		char const* name;
		// if present, this function is called when the setting is changed
		void (aux::session_impl::*fun)();
		char const *default_value;
#ifndef TORRENT_NO_DEPRECATE
		// offset into session_settings, used to map
		// settings to the deprecated settings struct
		int offset;
#endif
	};

	struct int_setting_entry_t
	{
		// the name of this setting. used for serialization and deserialization
		char const* name;
		// if present, this function is called when the setting is changed
		void (aux::session_impl::*fun)();
		int default_value;
#ifndef TORRENT_NO_DEPRECATE
		// offset into session_settings, used to map
		// settings to the deprecated settings struct
		int offset;
#endif
	};

	struct bool_setting_entry_t
	{
		// the name of this setting. used for serialization and deserialization
		char const* name;
		// if present, this function is called when the setting is changed
		void (aux::session_impl::*fun)();
		bool default_value;
#ifndef TORRENT_NO_DEPRECATE
		// offset into session_settings, used to map
		// settings to the deprecated settings struct
		int offset;
#endif
	};


// SET_NOPREV - this is used for new settings that don't exist in the
//              deprecated session_settings.

#ifdef TORRENT_NO_DEPRECATE
#define SET(name, default_value, fun) { #name, fun, default_value }
#define SET_NOPREV(name, default_value, fun) { #name, fun, default_value }
#define DEPRECATED_SET(name, default_value, fun) { "", NULL, 0 }
#else
#define SET(name, default_value, fun) { #name, fun, default_value, offsetof(libtorrent::session_settings, name) }
#define SET_NOPREV(name, default_value, fun) { #name, fun, default_value, 0 }
#define DEPRECATED_SET(name, default_value, fun) { #name, fun, default_value, offsetof(libtorrent::session_settings, name) }
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

	namespace {

	using aux::session_impl;

	str_setting_entry_t str_settings[settings_pack::num_string_settings] =
	{
		SET(user_agent, "libtorrent/" LIBTORRENT_VERSION, &session_impl::update_user_agent),
		SET(announce_ip, 0, 0),
		SET(mmap_cache, 0, 0),
		SET(handshake_client_version, 0, 0),
		SET_NOPREV(outgoing_interfaces, "", &session_impl::update_outgoing_interfaces),
		SET_NOPREV(listen_interfaces, "0.0.0.0:6881", &session_impl::update_listen_interfaces),
		SET_NOPREV(proxy_hostname, "", &session_impl::update_proxy),
		SET_NOPREV(proxy_username, "", &session_impl::update_proxy),
		SET_NOPREV(proxy_password, "", &session_impl::update_proxy),
		SET_NOPREV(i2p_hostname, "", &session_impl::update_i2p_bridge),
		SET_NOPREV(peer_fingerprint, "-LT1100-", &session_impl::update_peer_fingerprint)
	};

	bool_setting_entry_t bool_settings[settings_pack::num_bool_settings] =
	{
		SET(allow_multiple_connections_per_ip, false, 0),
		DEPRECATED_SET(ignore_limits_on_local_network, true, &session_impl::update_ignore_rate_limits_on_local_network),
		SET(send_redundant_have, true, 0),
		SET(lazy_bitfields, false, 0),
		SET(use_dht_as_fallback, false, 0),
		SET(upnp_ignore_nonrouters, false, 0),
		SET(use_parole_mode, true, 0),
		SET(use_read_cache, true, 0),
		DEPRECATED_SET(use_write_cache, true, 0),
		SET(dont_flush_write_cache, false, 0),
		DEPRECATED_SET(explicit_read_cache, false, 0),
		SET(coalesce_reads, false, 0),
		SET(coalesce_writes, false, 0),
		SET(auto_manage_prefer_seeds, false, 0),
		SET(dont_count_slow_torrents, true, &session_impl::update_count_slow),
		SET(close_redundant_connections, true, 0),
		SET(prioritize_partial_pieces, false, 0),
		SET(rate_limit_ip_overhead, true, 0),
		SET(announce_to_all_trackers, false, 0),
		SET(announce_to_all_tiers, false, 0),
		SET(prefer_udp_trackers, true, 0),
		SET(strict_super_seeding, false, 0),
		DEPRECATED_SET(lock_disk_cache, false, 0),
		SET(disable_hash_checks, false, 0),
		SET(allow_i2p_mixed, false, 0),
		SET(low_prio_disk, true, 0),
		SET(volatile_read_cache, false, 0),
		SET(guided_read_cache, false, 0),
		SET(no_atime_storage, true, 0),
		SET(incoming_starts_queued_torrents, false, 0),
		SET(report_true_downloaded, false, 0),
		SET(strict_end_game_mode, true, 0),
		SET(broadcast_lsd, true, 0),
		SET(enable_outgoing_utp, true, 0),
		SET(enable_incoming_utp, true, 0),
		SET(enable_outgoing_tcp, true, 0),
		SET(enable_incoming_tcp, true, 0),
		SET(ignore_resume_timestamps, false, 0),
		SET(no_recheck_incomplete_resume, false, 0),
		SET(anonymous_mode, false, &session_impl::update_anonymous_mode),
		SET(report_web_seed_downloads, true, &session_impl::update_report_web_seed_downloads),
		DEPRECATED_SET(rate_limit_utp, false, &session_impl::update_rate_limit_utp),
		SET(announce_double_nat, false, 0),
		SET(seeding_outgoing_connections, true, 0),
		SET(no_connect_privileged_ports, false, &session_impl::update_privileged_ports),
		SET(smooth_connects, true, 0),
		SET(always_send_user_agent, false, 0),
		SET(apply_ip_filter_to_trackers, true, 0),
		SET(use_disk_read_ahead, true, 0),
		SET(lock_files, false, 0),
		SET(contiguous_recv_buffer, true, 0),
		SET(ban_web_seeds, true, 0),
		SET_NOPREV(allow_partial_disk_writes, true, 0),
		SET(force_proxy, false, &session_impl::update_force_proxy),
		SET(support_share_mode, true, 0),
		SET(support_merkle_torrents, true, 0),
		SET(report_redundant_bytes, true, 0),
		SET_NOPREV(listen_system_port_fallback, true, 0),
		SET(use_disk_cache_pool, true, 0),
		SET_NOPREV(announce_crypto_support, true, 0),
		SET_NOPREV(enable_upnp, true, &session_impl::update_upnp),
		SET_NOPREV(enable_natpmp, true, &session_impl::update_natpmp),
		SET_NOPREV(enable_lsd, true, &session_impl::update_lsd),
		SET_NOPREV(enable_dht, true, &session_impl::update_dht),
		SET_NOPREV(prefer_rc4, false, 0),
		SET_NOPREV(proxy_hostnames, true, 0),
		SET_NOPREV(proxy_peer_connections, true, 0),
		SET_NOPREV(auto_sequential, true, &session_impl::update_auto_sequential),
		SET_NOPREV(proxy_tracker_connections, true, 0),
	};

	int_setting_entry_t int_settings[settings_pack::num_int_settings] =
	{
		SET(tracker_completion_timeout, 30, 0),
		SET(tracker_receive_timeout, 10, 0),
		SET(stop_tracker_timeout, 5, 0),
		SET(tracker_maximum_response_length, 1024*1024, 0),
		SET(piece_timeout, 20, 0),
		SET(request_timeout, 60, 0),
		SET(request_queue_time, 3, 0),
		SET(max_allowed_in_request_queue, 500, 0),
		SET(max_out_request_queue, 500, 0),
		SET(whole_pieces_threshold, 20, 0),
		SET(peer_timeout, 120, 0),
		SET(urlseed_timeout, 20, 0),
		SET(urlseed_pipeline_size, 5, 0),
		SET(urlseed_wait_retry, 30, 0),
		SET(file_pool_size, 40, 0),
		SET(max_failcount, 3, &session_impl::update_max_failcount),
		SET(min_reconnect_time, 60, 0),
		SET(peer_connect_timeout, 15, 0),
		SET(connection_speed, 10, &session_impl::update_connection_speed),
		SET(inactivity_timeout, 600, 0),
		SET(unchoke_interval, 15, 0),
		SET(optimistic_unchoke_interval, 30, 0),
		SET(num_want, 200, 0),
		SET(initial_picker_threshold, 4, 0),
		SET(allowed_fast_set_size, 10, 0),
		SET(suggest_mode, settings_pack::no_piece_suggestions, 0),
		SET(max_queued_disk_bytes, 1024 * 1024, &session_impl::update_queued_disk_bytes),
		SET(handshake_timeout, 10, 0),
		SET(send_buffer_low_watermark, 10 * 1024, 0),
		SET(send_buffer_watermark, 500 * 1024, 0),
		SET(send_buffer_watermark_factor, 50, 0),
		SET(choking_algorithm, settings_pack::fixed_slots_choker, 0),
		SET(seed_choking_algorithm, settings_pack::round_robin, 0),
		SET(cache_size, 1024, 0),
		SET(cache_buffer_chunk_size, 0, &session_impl::update_cache_buffer_chunk_size),
		SET(cache_expiry, 300, 0),
		DEPRECATED_SET(explicit_cache_interval, 30, 0),
		SET(disk_io_write_mode, settings_pack::enable_os_cache, 0),
		SET(disk_io_read_mode, settings_pack::enable_os_cache, 0),
		SET(outgoing_port, 0, 0),
		SET(num_outgoing_ports, 0, 0),
		SET(peer_tos, 0, &session_impl::update_peer_tos),
		SET(active_downloads, 3, &session_impl::trigger_auto_manage),
		SET(active_seeds, 5, &session_impl::trigger_auto_manage),
		SET_NOPREV(active_checking, 1, &session_impl::trigger_auto_manage),
		SET(active_dht_limit, 88, 0),
		SET(active_tracker_limit, 1600, 0),
		SET(active_lsd_limit, 60, 0),
		SET(active_limit, 15, &session_impl::trigger_auto_manage),
		SET_NOPREV(active_loaded_limit, 100, &session_impl::trigger_auto_manage),
		SET(auto_manage_interval, 30, 0),
		SET(seed_time_limit, 24 * 60 * 60, 0),
		SET(auto_scrape_interval, 1800, 0),
		SET(auto_scrape_min_interval, 300, 0),
		SET(max_peerlist_size, 3000, 0),
		SET(max_paused_peerlist_size, 1000, 0),
		SET(min_announce_interval, 5 * 60, 0),
		SET(auto_manage_startup, 60, 0),
		SET(seeding_piece_quota, 20, 0),
		SET(max_rejects, 50, 0),
		SET(recv_socket_buffer_size, 0, &session_impl::update_socket_buffer_size),
		SET(send_socket_buffer_size, 0, &session_impl::update_socket_buffer_size),
		SET(file_checks_delay_per_block, 0, 0),
		SET(read_cache_line_size, 32, 0),
		SET(write_cache_line_size, 16, 0),
		SET(optimistic_disk_retry, 10 * 60, 0),
		SET(max_suggest_pieces, 10, 0),
		SET(local_service_announce_interval, 5 * 60, 0),
		SET(dht_announce_interval, 15 * 60, &session_impl::update_dht_announce_interval),
		SET(udp_tracker_token_expiry, 60, 0),
		SET(default_cache_min_age, 1, 0),
		SET(num_optimistic_unchoke_slots, 0, 0),
		SET(default_est_reciprocation_rate, 16000, 0),
		SET(increase_est_reciprocation_rate, 20, 0),
		SET(decrease_est_reciprocation_rate, 3, 0),
		SET(max_pex_peers, 50, 0),
		SET(tick_interval, 500, 0),
		SET(share_mode_target, 3, 0),
		SET(upload_rate_limit, 0, &session_impl::update_upload_rate),
		SET(download_rate_limit, 0, &session_impl::update_download_rate),
		DEPRECATED_SET(local_upload_rate_limit, 0, &session_impl::update_local_upload_rate),
		DEPRECATED_SET(local_download_rate_limit, 0, &session_impl::update_local_download_rate),
		SET(dht_upload_rate_limit, 4000, &session_impl::update_dht_upload_rate_limit),
		SET(unchoke_slots_limit, 8, &session_impl::update_unchoke_limit),
		DEPRECATED_SET(half_open_limit, 0, 0),
		SET(connections_limit, 200, &session_impl::update_connections_limit),
		SET(connections_slack, 10, 0),
		SET(utp_target_delay, 100, 0),
		SET(utp_gain_factor, 3000, 0),
		SET(utp_min_timeout, 500, 0),
		SET(utp_syn_resends, 2, 0),
		SET(utp_fin_resends, 2, 0),
		SET(utp_num_resends, 3, 0),
		SET(utp_connect_timeout, 3000, 0),
		SET(utp_delayed_ack, 0, 0),
		SET(utp_loss_multiplier, 50, 0),
		SET(mixed_mode_algorithm, settings_pack::peer_proportional, 0),
		SET(listen_queue_size, 5, 0),
		SET(torrent_connect_boost, 10, 0),
		SET(alert_queue_size, 1000, &session_impl::update_alert_queue_size),
		SET(max_metadata_size, 3 * 1024 * 10240, 0),
		DEPRECATED_SET(hashing_threads, 1, 0),
		SET(checking_mem_usage, 256, 0),
		SET(predictive_piece_announce, 0, 0),
		SET(aio_threads, 4, &session_impl::update_disk_threads),
		SET(aio_max, 300, 0),
		SET(network_threads, 0, &session_impl::update_network_threads),
		SET(ssl_listen, 0, 0),
		SET(tracker_backoff, 250, 0),
		SET_NOPREV(share_ratio_limit, 200, 0),
		SET_NOPREV(seed_time_ratio_limit, 700, 0),
		SET_NOPREV(peer_turnover, 4, 0),
		SET_NOPREV(peer_turnover_cutoff, 90, 0),
		SET(peer_turnover_interval, 300, 0),
		SET_NOPREV(connect_seed_every_n_download, 10, 0),
		SET(max_http_recv_buffer_size, 4*1024*204, 0),
		SET_NOPREV(max_retry_port_bind, 10, 0),
		SET_NOPREV(alert_mask, alert::error_notification, &session_impl::update_alert_mask),
		SET_NOPREV(out_enc_policy, settings_pack::pe_enabled, 0),
		SET_NOPREV(in_enc_policy, settings_pack::pe_enabled, 0),
		SET_NOPREV(allowed_enc_level, settings_pack::pe_both, 0),
		SET(inactive_down_rate, 2048, 0),
		SET(inactive_up_rate, 2048, 0),
		SET_NOPREV(proxy_type, settings_pack::none, &session_impl::update_proxy),
		SET_NOPREV(proxy_port, 0, &session_impl::update_proxy),
		SET_NOPREV(i2p_port, 0, &session_impl::update_i2p_bridge),
		SET_NOPREV(cache_size_volatile, 256, 0)
	};

#undef SET

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	} // anonymous namespace

	int setting_by_name(std::string const& key)
	{
		for (int k = 0; k < sizeof(str_settings)/sizeof(str_settings[0]); ++k)
		{
			if (key != str_settings[k].name) continue;
			return settings_pack::string_type_base + k;
		}
		for (int k = 0; k < sizeof(int_settings)/sizeof(int_settings[0]); ++k)
		{
			if (key != int_settings[k].name) continue;
			return settings_pack::int_type_base + k;
		}
		for (int k = 0; k < sizeof(bool_settings)/sizeof(bool_settings[0]); ++k)
		{
			if (key != bool_settings[k].name) continue;
			return settings_pack::bool_type_base + k;
		}
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
		};
		return "";
	}

	boost::shared_ptr<settings_pack> load_pack_from_dict(bdecode_node const& settings)
	{
		boost::shared_ptr<settings_pack> pack = boost::make_shared<settings_pack>();

		for (int i = 0; i < settings.dict_size(); ++i)
		{
			std::string key;
			bdecode_node val;
			boost::tie(key, val) = settings.dict_at(i);
			switch (val.type())
			{
				case bdecode_node::dict_t:
				case bdecode_node::list_t:
					continue;
				case bdecode_node::int_t:
				{
					bool found = false;
					for (int k = 0; k < sizeof(int_settings)/sizeof(int_settings[0]); ++k)
					{
						if (key != int_settings[k].name) continue;
						pack->set_int(settings_pack::int_type_base + k, val.int_value());
						found = true;
						break;
					}
					if (found) continue;
					for (int k = 0; k < sizeof(bool_settings)/sizeof(bool_settings[0]); ++k)
					{
						if (key != bool_settings[k].name) continue;
						pack->set_bool(settings_pack::bool_type_base + k, val.int_value());
						break;
					}
				}
				break;
			case bdecode_node::string_t:
				for (int k = 0; k < sizeof(str_settings)/sizeof(str_settings[0]); ++k)
				{
					if (key != str_settings[k].name) continue;
					pack->set_str(settings_pack::string_type_base + k, val.string_value());
					break;
				}
				break;
			case bdecode_node::none_t:
				break;
			}
		}
		return pack;
	}

	void save_settings_to_dict(aux::session_settings const& s, entry::dictionary_type& sett)
	{
		// loop over all settings that differ from default
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			char const* cmp = str_settings[i].default_value == 0 ? "" : str_settings[i].default_value;
			if (cmp == s.m_strings[i]) continue;
			sett[str_settings[i].name] = s.m_strings[i];
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			if (int_settings[i].default_value == s.m_ints[i]) continue;
			sett[int_settings[i].name] = s.m_ints[i];
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			if (bool_settings[i].default_value == s.m_bools[i]) continue;
			sett[bool_settings[i].name] = s.m_bools[i];
		}
	}

#ifndef TORRENT_NO_DEPRECATE

#include "libtorrent/aux_/disable_warnings_push.hpp"

	boost::shared_ptr<settings_pack> load_pack_from_struct(
		aux::session_settings const& current, session_settings const& s)
	{
		boost::shared_ptr<settings_pack> p = boost::make_shared<settings_pack>();

		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			if (str_settings[i].offset == 0) continue;
			std::string& val = *(std::string*)(((char*)&s) + str_settings[i].offset);
			int setting_name = settings_pack::string_type_base + i;
			if (val == current.get_str(setting_name)) continue;
			p->set_str(setting_name, val);
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			if (int_settings[i].offset == 0) continue;
			int& val = *(int*)(((char*)&s) + int_settings[i].offset);
			int setting_name = settings_pack::int_type_base + i;
			if (val == current.get_int(setting_name)) continue;
			p->set_int(setting_name, val);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			if (bool_settings[i].offset == 0) continue;
			bool& val = *(bool*)(((char*)&s) + bool_settings[i].offset);
			int setting_name = settings_pack::bool_type_base + i;
			if (val == current.get_bool(setting_name)) continue;
			p->set_bool(setting_name, val);
		}

		// special case for deprecated float values
		int val = current.get_int(settings_pack::share_ratio_limit);
		if (fabs(s.share_ratio_limit - float(val) / 100.f) > 0.001f)
			p->set_int(settings_pack::share_ratio_limit, s.share_ratio_limit * 100);

		val = current.get_int(settings_pack::seed_time_ratio_limit);
		if (fabs(s.seed_time_ratio_limit - float(val) / 100.f) > 0.001f)
			p->set_int(settings_pack::seed_time_ratio_limit, s.seed_time_ratio_limit * 100);

		val = current.get_int(settings_pack::peer_turnover);
		if (fabs(s.peer_turnover - float(val) / 100.f) > 0.001)
			p->set_int(settings_pack::peer_turnover, s.peer_turnover * 100);

		val = current.get_int(settings_pack::peer_turnover_cutoff);
		if (fabs(s.peer_turnover_cutoff - float(val) / 100.f) > 0.001)
			p->set_int(settings_pack::peer_turnover_cutoff, s.peer_turnover_cutoff * 100);

		return p;
	}

	void load_struct_from_settings(aux::session_settings const& current, session_settings& ret)
	{
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			if (str_settings[i].offset == 0) continue;
			std::string& val = *(std::string*)(((char*)&ret) + str_settings[i].offset);
			val = current.get_str(settings_pack::string_type_base + i);
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			if (int_settings[i].offset == 0) continue;
			int& val = *(int*)(((char*)&ret) + int_settings[i].offset);
			val = current.get_int(settings_pack::int_type_base + i);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			if (bool_settings[i].offset == 0) continue;
			bool& val = *(bool*)(((char*)&ret) + bool_settings[i].offset);
			val = current.get_bool(settings_pack::bool_type_base + i);
		}

		// special case for deprecated float values
		ret.share_ratio_limit = float(current.get_int(settings_pack::share_ratio_limit)) / 100.f;
		ret.seed_time_ratio_limit = float(current.get_int(settings_pack::seed_time_ratio_limit)) / 100.f;
		ret.peer_turnover = float(current.get_int(settings_pack::peer_turnover)) / 100.f;
		ret.peer_turnover_cutoff = float(current.get_int(settings_pack::peer_turnover_cutoff)) / 100.f;
	}

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif

	void initialize_default_settings(aux::session_settings& s)
	{
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			if (str_settings[i].default_value == 0) continue;
			s.set_str(settings_pack::string_type_base + i, str_settings[i].default_value);
			TORRENT_ASSERT(s.get_str(settings_pack::string_type_base + i) == str_settings[i].default_value);
		}

		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			s.set_int(settings_pack::int_type_base + i, int_settings[i].default_value);
			TORRENT_ASSERT(s.get_int(settings_pack::int_type_base + i) == int_settings[i].default_value);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			s.set_bool(settings_pack::bool_type_base + i, bool_settings[i].default_value);
			TORRENT_ASSERT(s.get_bool(settings_pack::bool_type_base + i) == bool_settings[i].default_value);
		}

		// this seems questionable...
/*
		// Some settings have dynamic defaults depending on the machine
		// for instance, the disk cache size

		// by default, set the cahe size to an 8:th of the total amount of physical RAM
		boost::uint64_t phys_ram = total_physical_ram();
		if (phys_ram > 0) s.set_int(settings_pack::cache_size, phys_ram / 16 / 1024 / 8);
*/
	}

	void apply_pack(settings_pack const* pack, aux::session_settings& sett
		, aux::session_impl* ses)
	{
		typedef void (aux::session_impl::*fun_t)();
		std::vector<fun_t> callbacks;

		for (std::vector<std::pair<boost::uint16_t, std::string> >::const_iterator i = pack->m_strings.begin()
			, end(pack->m_strings.end()); i != end; ++i)
		{
			// disregard setting indices that are not string types
			if ((i->first & settings_pack::type_mask) != settings_pack::string_type_base)
				continue;

			// ignore settings that are out of bounds
			int index = i->first & settings_pack::index_mask;
			if (index < 0 || index >= settings_pack::num_string_settings)
				continue;

			sett.set_str(i->first, i->second);
			str_setting_entry_t const& sa = str_settings[i->first & settings_pack::index_mask];
			if (sa.fun && ses
				&& std::find(callbacks.begin(), callbacks.end(), sa.fun) == callbacks.end())
				callbacks.push_back(sa.fun);
		}

		for (std::vector<std::pair<boost::uint16_t, int> >::const_iterator i = pack->m_ints.begin()
			, end(pack->m_ints.end()); i != end; ++i)
		{
			// disregard setting indices that are not int types
			if ((i->first & settings_pack::type_mask) != settings_pack::int_type_base)
				continue;

			// ignore settings that are out of bounds
			int index = i->first & settings_pack::index_mask;
			if (index < 0 || index >= settings_pack::num_int_settings)
				continue;

			sett.set_int(i->first, i->second);
			int_setting_entry_t const& sa = int_settings[i->first & settings_pack::index_mask];
			if (sa.fun && ses
				&& std::find(callbacks.begin(), callbacks.end(), sa.fun) == callbacks.end())
				callbacks.push_back(sa.fun);
		}

		for (std::vector<std::pair<boost::uint16_t, bool> >::const_iterator i = pack->m_bools.begin()
			, end(pack->m_bools.end()); i != end; ++i)
		{
			// disregard setting indices that are not bool types
			if ((i->first & settings_pack::type_mask) != settings_pack::bool_type_base)
				continue;

			// ignore settings that are out of bounds
			int index = i->first & settings_pack::index_mask;
			if (index < 0 || index >= settings_pack::num_bool_settings)
				continue;

			sett.set_bool(i->first, i->second);
			bool_setting_entry_t const& sa = bool_settings[i->first & settings_pack::index_mask];
			if (sa.fun && ses
				&& std::find(callbacks.begin(), callbacks.end(), sa.fun) == callbacks.end())
				callbacks.push_back(sa.fun);
		}

		// call the callbacks once all the settings have been applied, and
		// only once per callback
		for (std::vector<fun_t>::iterator i = callbacks.begin(), end(callbacks.end());
			i != end; ++i)
		{
			fun_t const& f = *i;
			(ses->*f)();
		}
	}

	void settings_pack::set_str(int name, std::string val)
	{
		TORRENT_ASSERT((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return;
		std::pair<boost::uint16_t, std::string> v(name, val);
		insort_replace(m_strings, v);
	}

	void settings_pack::set_int(int name, int val)
	{
		TORRENT_ASSERT((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return;
		std::pair<boost::uint16_t, int> v(name, val);
		insort_replace(m_ints, v);
	}

	void settings_pack::set_bool(int name, bool val)
	{
		TORRENT_ASSERT((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return;
		std::pair<boost::uint16_t, bool> v(name, val);
		insort_replace(m_bools, v);
	}

	bool settings_pack::has_val(int name) const
	{
		switch (name & type_mask)
		{
			case string_type_base:
			{
				// this is an optimization. If the settings pack is complete,
				// i.e. has every key, we don't need to search, it's just a lookup
				if (m_strings.size() == settings_pack::num_string_settings)
					return true;
				std::pair<boost::uint16_t, std::string> v(name, std::string());
				std::vector<std::pair<boost::uint16_t, std::string> >::const_iterator i =
					std::lower_bound(m_strings.begin(), m_strings.end(), v
						, &compare_first<std::string>);
				return i != m_strings.end() && i->first == name;
			}
			case int_type_base:
			{
				// this is an optimization. If the settings pack is complete,
				// i.e. has every key, we don't need to search, it's just a lookup
				if (m_ints.size() == settings_pack::num_int_settings)
					return true;
				std::pair<boost::uint16_t, int> v(name, 0);
				std::vector<std::pair<boost::uint16_t, int> >::const_iterator i =
					std::lower_bound(m_ints.begin(), m_ints.end(), v
						, &compare_first<int>);
				return i != m_ints.end() && i->first == name;
			}
			case bool_type_base:
			{
				// this is an optimization. If the settings pack is complete,
				// i.e. has every key, we don't need to search, it's just a lookup
				if (m_bools.size() == settings_pack::num_bool_settings)
					return true;
				std::pair<boost::uint16_t, bool> v(name, false);
				std::vector<std::pair<boost::uint16_t, bool> >::const_iterator i =
					std::lower_bound(m_bools.begin(), m_bools.end(), v
						, &compare_first<bool>);
				return i != m_bools.end() && i->first == name;
			}
		}
		TORRENT_ASSERT(false);
		return false;
	}

	std::string settings_pack::get_str(int name) const
	{
		TORRENT_ASSERT((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return std::string();

		// this is an optimization. If the settings pack is complete,
		// i.e. has every key, we don't need to search, it's just a lookup
		if (m_strings.size() == settings_pack::num_string_settings)
		{
			TORRENT_ASSERT(m_strings[name & index_mask].first == name);
			return m_strings[name & index_mask].second;
		}
		std::pair<boost::uint16_t, std::string> v(name, std::string());
		std::vector<std::pair<boost::uint16_t, std::string> >::const_iterator i
			= std::lower_bound(m_strings.begin(), m_strings.end(), v
				, &compare_first<std::string>);
		if (i != m_strings.end() && i->first == name) return i->second;
		return std::string();
	}

	int settings_pack::get_int(int name) const
	{
		TORRENT_ASSERT((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return 0;

		// this is an optimization. If the settings pack is complete,
		// i.e. has every key, we don't need to search, it's just a lookup
		if (m_ints.size() == settings_pack::num_int_settings)
		{
			TORRENT_ASSERT(m_ints[name & index_mask].first == name);
			return m_ints[name & index_mask].second;
		}
		std::pair<boost::uint16_t, int> v(name, 0);
		std::vector<std::pair<boost::uint16_t, int> >::const_iterator i
			= std::lower_bound(m_ints.begin(), m_ints.end(), v
				, &compare_first<int>);
		if (i != m_ints.end() && i->first == name) return i->second;
		return 0;
	}

	bool settings_pack::get_bool(int name) const
	{
		TORRENT_ASSERT((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return false;

		// this is an optimization. If the settings pack is complete,
		// i.e. has every key, we don't need to search, it's just a lookup
		if (m_bools.size() == settings_pack::num_bool_settings)
		{
			TORRENT_ASSERT(m_bools[name & index_mask].first == name);
			return m_bools[name & index_mask].second;
		}
		std::pair<boost::uint16_t, bool> v(name, false);
		std::vector<std::pair<boost::uint16_t, bool> >::const_iterator i
			= std::lower_bound(m_bools.begin(), m_bools.end(), v
					, &compare_first<bool>);
		if (i != m_bools.end() && i->first == name) return i->second;
		return false;
	}

	void settings_pack::clear()
	{
		m_strings.clear();
		m_ints.clear();
		m_bools.clear();
	}
}

