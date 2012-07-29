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

#include "libtorrent/config.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/session_impl.hpp"

namespace {

	template <class T>
	void insort_replace(std::vector<std::pair<int, T> >& c, std::pair<int, T> const& v)
	{
		typedef std::vector<std::pair<int, T> > container_t;
		typename container_t::iterator i = std::lower_bound(c.begin(), c.end(), v);
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
#define DEPRECATED_SET(name, default_value, fun) { "", NULL, NULL }
#else
#define SET(name, default_value, fun) { #name, fun, default_value, offsetof(libtorrent::session_settings, name) }
#define SET_NOPREV(name, default_value, fun) { #name, fun, default_value, 0 }
#define DEPRECATED_SET(name, default_value, fun) { #name, fun, default_value, offsetof(libtorrent::session_settings, name) }
#endif

	using aux::session_impl;

	str_setting_entry_t str_settings[settings_pack::num_string_settings] =
	{
		SET(user_agent, "libtorrent/"LIBTORRENT_VERSION, &session_impl::update_user_agent),
		SET(announce_ip, 0, 0),
		SET(mmap_cache, 0, 0),
	};

	bool_setting_entry_t bool_settings[settings_pack::num_bool_settings] =
	{
		SET(allow_multiple_connections_per_ip, false, 0),
		DEPRECATED_SET(ignore_limits_on_local_network, true, &session_impl::update_ignore_rate_limits_on_local_network),
		SET(send_redundant_have, true, 0),
		SET(lazy_bitfields, true, 0),
		SET(use_dht_as_fallback, false, 0),
		SET(upnp_ignore_nonrouters, false, 0),
		SET(use_parole_mode, true, 0),
		SET(use_read_cache, true, 0),
		SET(use_write_cache, true, 0),
		SET(dont_flush_write_cache, false, 0),
		SET(explicit_read_cache, false, 0),
		SET(coalesce_reads, false, 0),
		SET(coalesce_writes, false, 0),
		SET(auto_manage_prefer_seeds, false, 0),
		SET(dont_count_slow_torrents, true, 0),
		SET(close_redundant_connections, true, 0),
		SET(prioritize_partial_pieces, false, 0),
		SET(rate_limit_ip_overhead, true, 0),
		SET(announce_to_all_trackers, false, 0),
		SET(announce_to_all_tiers, false, 0),
		SET(prefer_udp_trackers, true, 0),
		SET(strict_super_seeding, false, 0),
		SET(lock_disk_cache, false, 0),
		SET(optimize_hashing_for_speed, true, 0),
		SET(disable_hash_checks, false, 0),
		SET(allow_reordered_disk_operations, true, 0),
		SET(allow_i2p_mixed, false, 0),
		SET(drop_skipped_requests, false, 0),
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
		SET(anonymous_mode, false, 0),
		SET(report_web_seed_downloads, true, &session_impl::update_report_web_seed_downloads),
		SET(utp_dynamic_sock_buf, true, 0),
		DEPRECATED_SET(rate_limit_utp, false, &session_impl::update_rate_limit_utp),
		SET(announce_double_nat, false, 0),
		SET(seeding_outgoing_connections, true, 0),
		SET(no_connect_privileged_ports, true, 0),
		SET(smooth_connects, true, 0),
		SET(always_send_user_agent, false, 0),
		SET(apply_ip_filter_to_trackers, true, 0),
		SET(use_disk_read_ahead, true, 0),
		SET(lock_files, false, 0),
		SET(contiguous_recv_buffer, true, 0),
		SET(ban_web_seeds, true, 0),
	};

	int_setting_entry_t int_settings[settings_pack::num_int_settings] =
	{
		SET(tracker_completion_timeout, 60, 0),
		SET(tracker_receive_timeout, 40, 0),
		SET(stop_tracker_timeout, 5, 0),
		SET(tracker_maximum_response_length, 1024*1024, 0),
		SET(piece_timeout, 20, 0),
		SET(request_timeout, 50, 0),
		SET(request_queue_time, 3, 0),
		SET(max_allowed_in_request_queue, 250, 0),
		SET(max_out_request_queue, 200, 0),
		SET(whole_pieces_threshold, 20, 0),
		SET(peer_timeout, 120, 0),
		SET(urlseed_timeout, 20, 0),
		SET(urlseed_pipeline_size, 5, 0),
		SET(urlseed_wait_retry, 30, 0),
		SET(file_pool_size, 40, 0),
		SET(max_failcount, 3, 0),
		SET(min_reconnect_time, 60, 0),
		SET(peer_connect_timeout, 15, 0),
		SET(connection_speed, 6, &session_impl::update_connection_speed),
		SET(inactivity_timeout, 600, 0),
		SET(unchoke_interval, 15, 0),
		SET(optimistic_unchoke_interval, 30, 0),
		SET(num_want, 200, 0),
		SET(initial_picker_threshold, 4, 0),
		SET(allowed_fast_set_size, 10, 0),
		SET(suggest_mode, settings_pack::no_piece_suggestions, 0),
		SET(max_queued_disk_bytes, 1024 * 1024, 0),
		SET(handshake_timeout, 10, 0),
		SET(send_buffer_low_watermark, 512, 0),
		SET(send_buffer_watermark, 500 * 1024, 0),
		SET(send_buffer_watermark_factor, 50, 0),
		SET(choking_algorithm, settings_pack::fixed_slots_choker, &session_impl::update_choking_algorithm),
		SET(seed_choking_algorithm, settings_pack::round_robin, 0),
		SET(cache_size, 1024, 0),
		SET(cache_buffer_chunk_size, 0, &session_impl::update_cache_buffer_chunk_size),
		SET(cache_expiry, 300, 0),
		SET(explicit_cache_interval, 30, 0),
		SET(disk_io_write_mode, settings_pack::enable_os_cache, 0),
		SET(disk_io_read_mode, settings_pack::enable_os_cache, 0),
		SET(outgoing_port, 0, 0),
		SET(num_outgoing_ports, 0, 0),
		SET(peer_tos, 0, &session_impl::update_peer_tos),
		SET(active_downloads, 3, &session_impl::reset_auto_manage_timer),
		SET(active_seeds, 5, &session_impl::reset_auto_manage_timer),
		SET(active_dht_limit, 88, 0),
		SET(active_tracker_limit, 360, 0),
		SET(active_lsd_limit, 60, 0),
		SET(active_limit, 15, &session_impl::reset_auto_manage_timer),
		SET_NOPREV(active_loaded_limit, 0, &session_impl::reset_auto_manage_timer),
		SET(auto_manage_interval, 30, 0),
		SET(seed_time_limit, 24 * 60 * 60, 0),
		SET(auto_scrape_interval, 1800, 0),
		SET(auto_scrape_min_interval, 300, 0),
		SET(max_peerlist_size, 3000, 0),
		SET(max_paused_peerlist_size, 1000, 0),
		SET(min_announce_interval, 5 * 60, 0),
		SET(auto_manage_startup, 120, 0),
		SET(seeding_piece_quota, 20, 0),
#ifdef TORRENT_WINDOWS
		SET(max_sparse_regions, 30000, 0),
#else
		SET(max_sparse_regions, 0, 0),
#endif
		SET(max_rejects, 50, 0),
		SET(recv_socket_buffer_size, 0, 0),
		SET(send_socket_buffer_size, 0, 0),
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
		SET(tick_interval, 100, 0),
		SET(share_mode_target, 3, 0),
		SET(upload_rate_limit, 0, &session_impl::update_upload_rate),
		SET(download_rate_limit, 0, &session_impl::update_download_rate),
		DEPRECATED_SET(local_upload_rate_limit, 0, &session_impl::update_local_upload_rate),
		DEPRECATED_SET(local_download_rate_limit, 0, &session_impl::update_local_download_rate),
		SET(dht_upload_rate_limit, 4000, &session_impl::update_dht_upload_rate_limit),
		SET(unchoke_slots_limit, 8, &session_impl::update_choking_algorithm),
		SET(half_open_limit, 0, &session_impl::update_half_open),
		SET(connections_limit, 200, &session_impl::update_connections_limit),
		SET(utp_target_delay, 100, 0),
		SET(utp_gain_factor, 1500, 0),
		SET(utp_min_timeout, 500, 0),
		SET(utp_syn_resends, 2, 0),
		SET(utp_fin_resends, 2, 0),
		SET(utp_num_resends, 6, 0),
		SET(utp_connect_timeout, 3000, 0),
		SET(utp_delayed_ack, 0, 0),
		SET(utp_loss_multiplier, 50, 0),
		SET(mixed_mode_algorithm, settings_pack::peer_proportional, 0),
		SET(listen_queue_size, 5, 0),
		SET(torrent_connect_boost, 10, 0),
		SET(alert_queue_size, 1000, &session_impl::update_alert_queue_size),
		SET(max_metadata_size, 3 * 1024 * 10240, 0),
		SET(read_job_every, 10, 0),
		SET(hashing_threads, 1, 0),
		SET(checking_mem_usage, 256, 0),
		SET(predictive_piece_announce, 0, 0),
		SET(aio_threads, 4, &session_impl::update_disk_threads),
		SET(aio_max, 300, 0),
		// multiple network threads won't work until udp_socket supports multi threading
		SET(network_threads, 0, &session_impl::update_network_threads),
		SET(ssl_listen, 4433, 0),
		SET(tracker_backoff, 250, 0),
		SET(share_ratio_limit, 200, 0),
		SET(seed_time_ratio_limit, 700, 0),
		SET(peer_turnover, 4, 0),
		SET(peer_turnover_cutoff, 90, 0),
		SET(peer_turnover_interval, 300, 0),
		SET_NOPREV(connect_seed_every_n_download, 10, 0)
	};

#undef SET

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

	settings_pack* load_pack_from_dict(lazy_entry const* settings)
	{
		settings_pack* pack = new settings_pack;

		for (int i = 0; i < settings->dict_size(); ++i)
		{
			std::string key;
			lazy_entry const* val;
			boost::tie(key, val) = settings->dict_at(i);
			switch (val->type())
			{
				case lazy_entry::dict_t:
				case lazy_entry::list_t:
					continue;
				case lazy_entry::int_t:
				{
					bool found = false;
					for (int k = 0; k < sizeof(int_settings)/sizeof(int_settings[0]); ++k)
					{
						if (key != int_settings[k].name) continue;
						pack->set_int(settings_pack::int_type_base + k, val->int_value());
						found = true;
						break;
					}
					if (found) continue;
					for (int k = 0; k < sizeof(bool_settings)/sizeof(bool_settings[0]); ++k)
					{
						if (key != bool_settings[k].name) continue;
						pack->set_bool(settings_pack::bool_type_base + k, val->int_value());
						break;
					}
				}
				break;
			case lazy_entry::string_t:
				for (int k = 0; k < sizeof(str_settings)/sizeof(str_settings[0]); ++k)
				{
					if (key != str_settings[k].name) continue;
					pack->set_str(settings_pack::string_type_base + k, val->string_value());
					break;
				}
				break;
			case lazy_entry::none_t:
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
	settings_pack* load_pack_from_struct(aux::session_settings const& current, session_settings const& s)
	{
		settings_pack* p = new settings_pack;

		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			std::string& val = *(std::string*)(((char*)&s) + str_settings[i].offset);
			int setting_name = settings_pack::string_type_base + i;
			if (val == current.get_str(setting_name)) continue;
			p->set_str(setting_name, val);
		}
	
		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			int& val = *(int*)(((char*)&s) + int_settings[i].offset);
			int setting_name = settings_pack::int_type_base + i;
			if (val == current.get_int(setting_name)) continue;
			p->set_int(setting_name, val);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			bool& val = *(bool*)(((char*)&s) + bool_settings[i].offset);
			int setting_name = settings_pack::bool_type_base + i;
			if (val == current.get_bool(setting_name)) continue;
			p->set_bool(setting_name, val);
		}

		return p;
	}

	void load_struct_from_settings(aux::session_settings const& current, session_settings& ret)
	{
		for (int i = 0; i < settings_pack::num_string_settings; ++i)
		{
			std::string& val = *(std::string*)(((char*)&ret) + str_settings[i].offset);
			val = current.get_str(settings_pack::string_type_base + i);
		}
	
		for (int i = 0; i < settings_pack::num_int_settings; ++i)
		{
			int& val = *(int*)(((char*)&ret) + int_settings[i].offset);
			val = current.get_int(settings_pack::int_type_base + i);
		}

		for (int i = 0; i < settings_pack::num_bool_settings; ++i)
		{
			bool& val = *(bool*)(((char*)&ret) + bool_settings[i].offset);
			val = current.get_bool(settings_pack::bool_type_base + i);
		}
	}
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
	}

	void apply_pack(settings_pack const* pack, aux::session_settings& sett, aux::session_impl* ses)
	{
		for (std::vector<std::pair<int, std::string> >::const_iterator i = pack->m_strings.begin()
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
			if (sa.fun && ses) (ses->*sa.fun)();
		}
	
		for (std::vector<std::pair<int, int> >::const_iterator i = pack->m_ints.begin()
			, end(pack->m_ints.end()); i != end; ++i)
		{
			// disregard setting indices that are not string types
			if ((i->first & settings_pack::type_mask) != settings_pack::int_type_base)
				continue;
		
			// ignore settings that are out of bounds
			int index = i->first & settings_pack::index_mask;
			if (index < 0 || index >= settings_pack::num_int_settings)
				continue;

			sett.set_int(i->first, i->second);
			int_setting_entry_t const& sa = int_settings[i->first & settings_pack::index_mask];
			if (sa.fun && ses) (ses->*sa.fun)();
		}

		for (std::vector<std::pair<int, bool> >::const_iterator i = pack->m_bools.begin()
			, end(pack->m_bools.end()); i != end; ++i)
		{
			// disregard setting indices that are not string types
			if ((i->first & settings_pack::type_mask) != settings_pack::bool_type_base)
				continue;
		
			// ignore settings that are out of bounds
			int index = i->first & settings_pack::index_mask;
			if (index < 0 || index >= settings_pack::num_bool_settings)
				continue;

			sett.set_bool(i->first, i->second);
			bool_setting_entry_t const& sa = bool_settings[i->first & settings_pack::index_mask];
			if (sa.fun && ses) (ses->*sa.fun)();
		}
	}

	void settings_pack::set_str(int name, std::string val)
	{
		TORRENT_ASSERT((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return;
		std::pair<int, std::string> v(name, val);
		insort_replace(m_strings, v);
	}

	void settings_pack::set_int(int name, int val)
	{
		TORRENT_ASSERT((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return;
		std::pair<int, int> v(name, val);
		insort_replace(m_ints, v);
	}

	void settings_pack::set_bool(int name, bool val)
	{
		TORRENT_ASSERT((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return;
		std::pair<int, bool> v(name, val);
		insort_replace(m_bools, v);
	}

	std::string settings_pack::get_str(int name) const
	{
		TORRENT_ASSERT((name & type_mask) == string_type_base);
		if ((name & type_mask) != string_type_base) return std::string();

		std::pair<int, std::string> v(name, std::string());
		std::vector<std::pair<int, std::string> >::const_iterator i
			= std::lower_bound(m_strings.begin(), m_strings.end(), v);
		if (i != m_strings.end() && i->first == name) return i->second;
		return std::string();
	}

	int settings_pack::get_int(int name) const
	{
		TORRENT_ASSERT((name & type_mask) == int_type_base);
		if ((name & type_mask) != int_type_base) return 0;

		std::pair<int, int> v(name, 0);
		std::vector<std::pair<int, int> >::const_iterator i
			= std::lower_bound(m_ints.begin(), m_ints.end(), v);
		if (i != m_ints.end() && i->first == name) return i->second;
		return 0;
	}

	bool settings_pack::get_bool(int name) const
	{
		TORRENT_ASSERT((name & type_mask) == bool_type_base);
		if ((name & type_mask) != bool_type_base) return false;

		std::pair<int, bool> v(name, false);
		std::vector<std::pair<int, bool> >::const_iterator i
			= std::lower_bound(m_bools.begin(), m_bools.end(), v);
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

