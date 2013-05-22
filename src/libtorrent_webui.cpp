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

#include "libtorrent_webui.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/alert_handler.hpp"
#include "local_mongoose.h"
#include "auth.hpp"
#include "torrent_history.hpp"
#include <string.h>

namespace libtorrent
{
	namespace io = libtorrent::detail;

	libtorrent_webui::libtorrent_webui(session& ses, torrent_history const* hist, auth_interface const* auth, alert_handler* alert)
		: m_ses(ses)
		, m_hist(hist)
		, m_auth(auth)
		, m_alert(alert)
	{}

	libtorrent_webui::~libtorrent_webui() {}

	bool libtorrent_webui::handle_websocket_connect(mg_connection* conn,
		mg_request_info const* request_info)
	{
		// we only provide access to /bt/control
		if (strcmp("/bt/control", request_info->uri) != 0) return false;

		// authenticate
/*		permissions_interface const* perms = parse_http_auth(conn, m_auth);
		if (!perms)
		{
			mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
				"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
				"Content-Length: 0\r\n\r\n");
			return true;
		}
*/
		return websocket_handler::handle_websocket_connect(conn, request_info);
	}

	struct rpc_entry
	{
		char const* name;
		bool (libtorrent_webui::*handler)(libtorrent_webui::conn_state*);
	};

	rpc_entry functions[] =
	{
		{ "get-torrent-updates", &libtorrent_webui::get_torrent_updates },
		{ "start", &libtorrent_webui::start },
		{ "stop", &libtorrent_webui::stop },
		{ "set-auto-managed", &libtorrent_webui::set_auto_managed },
		{ "clear-auto-managed", &libtorrent_webui::clear_auto_managed },
		{ "queue-up", &libtorrent_webui::queue_up },
		{ "queue-down", &libtorrent_webui::queue_down },
		{ "queue-top", &libtorrent_webui::queue_top },
		{ "queue-bottom", &libtorrent_webui::queue_bottom },
		{ "remove", &libtorrent_webui::remove },
		{ "remove_and_data", &libtorrent_webui::remove_and_data },
		{ "force_recheck", &libtorrent_webui::force_recheck },
		{ "set-sequential-download", &libtorrent_webui::set_sequential_download },
		{ "clear-sequential-download", &libtorrent_webui::clear_sequential_download },
		{ "list-settings", &libtorrent_webui::list_settings },
		{ "get-settings", &libtorrent_webui::get_settings },
		{ "set-settings", &libtorrent_webui::set_settings },
		{ "list-stats", &libtorrent_webui::list_stats },
		{ "get-stats", &libtorrent_webui::get_stats },
	};

	// maps torrent field to RPC field. These fields are the ones defined in
	// torrent_history_entry
	int torrent_field_map[] =
	{
		20, // state
		0, // paused
		0, // auto_managed
		0, // sequential_download
		0, // is_seeding
		0, // is_finished
		0, // is_loaded
		0, // has_metadata
		-1, // progress
		8, // progress_ppm
		9, // error
		-1, // save_path
		1, // name
		-1, // next_announce
		-1, // announce_interval
		-1, // current_tracker
		3, // total_download
		2, // total_upload
		-1, // total_payload_download
		-1, // total_payload_upload
		21, // total_failed_bytes
		22, // total_redundant_bytes
		7, // download_rate
		6, // upload_rate
		-1, // download_payload_rate
		-1, // upload_payload_rate
		11, // num_seeds
		10, // num_peers
		-1, // num_complete
		-1, // num_incomplete
		-1, // list_seeds
		-1, // list_peers
		-1, // connect_candidates
		12, // num_pieces
		-1, // total_done
		13, // total_wanted_done
		-1, // total_wanted
		14, // distributed_full_copies
		14, // distributed_fraction
		-1, // distributed_copies
		-1, // block_size
		17, // num_uploads
		18, // num_connections
		-1, // num_undead_peers
		-1, // uploads_limit
		-1, // connections_limit
		-1, // storage_mode
		-1, // up_bandwidth_queue
		-1, // down_bandwidth_queue
		15, // all_time_upload
		16, // all_time_download
		-1, // active_time
		-1, // finished_time
		-1, // seeding_time
		-1, // seed_rank
		-1, // last_scrape
		-1, // has_incoming
		-1, // sparse_regions
		0, // seed_mode
		0, // upload_mode
		0, // share_mode
		0, // super_seeding
		-1, // priority
		4, // added_time
		5, // completed_time
		-1, // last_seen_complete
		-1, // time_since_upload
		-1, // time_since_download
		19, // queue_position
		-1, // need_save_resume
		-1, // ip_filter_applies
		-1, // listen_port
	};

	// this is one of the key functions in the interface. It goes to
	// some length to ensure we only send relevant information back,
	// and in a compact format
	bool libtorrent_webui::get_torrent_updates(conn_state* st)
	{
		if (st->len < 12) return error(st, truncated_message);

		boost::uint32_t frame = io::read_uint32(st->data);
		boost::uint64_t user_mask = io::read_uint64(st->data);
		st->len -= 12;

		std::vector<torrent_history_entry> torrents;
		m_hist->updated_fields_since(frame, torrents);

		std::vector<sha1_hash> removed_torrents;
		m_hist->removed_since(frame, removed_torrents);

		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(no_error, ptr);

		// frame number (uint32)
		io::write_uint32(m_hist->frame(), ptr);

		// allocate space for torrent count
		// this will be filled in later when we know
		int num_torrents = 0;
		int num_torrents_pos = response.size();
		io::write_uint32(num_torrents, ptr);

		io::write_uint32(removed_torrents.size(), ptr);

		for (std::vector<torrent_history_entry>::iterator i = torrents.begin()
			, end(torrents.end()); i != end; ++i)
		{
			boost::uint64_t bitmask = 0;

			// look at which fields actually have a newer frame number
			// than the caller. Don't return fields that haven't changed.
			for (int k = 0; k < torrent_history_entry::num_fields; ++k)
			{
				int f = torrent_field_map[k];
				if (f < 0) continue;
				if (i->frame[k] <= frame) continue;

				// this field has changed and should be included in this update
				bitmask |= 1 << f;
			}

			// only return fields the caller asked for
			bitmask &= user_mask;

			if (bitmask == 0) continue;

			++num_torrents;
			// first write the info-hash
			std::copy(i->status.info_hash.begin(), i->status.info_hash.end(), ptr);
			// then 64 bits of bitmask, indicating which fields
			// are included in the update for this torrent
			io::write_uint64(bitmask, ptr);

			torrent_status const& s = i->status;

			for (int f = 0; f < 23; ++f)
			{
				if ((bitmask & (1 << f)) == 0) continue;

				// write field f to buffer
				switch (f)
				{
					case 0: // flags
					{
						boost::uint64_t flags = 
							(s.paused ? 0x001 : 0)
							| (s.auto_managed ? 0x002 : 0)
							| (s.sequential_download ? 0x004 : 0)
							| (s.is_seeding ? 0x008 : 0)
							| (s.is_finished ? 0x010 : 0)
							| (s.is_loaded ? 0x020 : 0)
							| (s.has_metadata ? 0x040 : 0)
							| (s.has_incoming ? 0x080 : 0)
							| (s.seed_mode ? 0x100 : 0)
							| (s.upload_mode ? 0x200 : 0)
							| (s.share_mode ? 0x400 : 0)
							| (s.super_seeding ? 0x800 : 0)
							;

						io::write_uint64(flags, ptr);
						break;
					}
					case 1: // name
					{
						std::string name = s.name;
						if (name.size() > 65535) name.resize(65535);
						io::write_uint16(name.size(), ptr);
						std::copy(name.begin(), name.end(), ptr);
						break;
					}
					case 2: // total-uploaded
						io::write_uint64(s.total_upload, ptr);
						break;
					case 3: // total-downloaded
						io::write_uint64(s.total_download, ptr);
						break;
					case 4: // added-time
						io::write_uint64(s.added_time, ptr);
						break;
					case 5: // completed_time
						io::write_uint64(s.completed_time, ptr);
						break;
					case 6: // upload-rate
						io::write_uint32(s.upload_rate, ptr);
						break;
					case 7: // download-rate
						io::write_uint32(s.download_rate, ptr);
						break;
					case 8: // progress
						io::write_uint32(s.progress_ppm, ptr);
						break;
					case 9: // error
					{
						std::string e = s.error;
						if (e.size() > 65535) e.resize(65535);
						io::write_uint16(e.size(), ptr);
						std::copy(e.begin(), e.end(), ptr);
						break;
					}
					case 10: // connected-peers
						io::write_uint32(s.num_peers, ptr);
						break;
					case 11: // connected-seeds
						io::write_uint32(s.num_seeds, ptr);
						break;
					case 12: // downloaded-pieces
						io::write_uint32(s.num_pieces, ptr);
						break;
					case 13: // total-done
						io::write_uint64(s.total_wanted_done, ptr);
						break;
					case 14: // distributed-copies
						io::write_uint32(s.distributed_full_copies, ptr);
						io::write_uint32(s.distributed_fraction, ptr);
						break;
					case 15: // all-time-upload
						io::write_uint64(s.all_time_upload, ptr);
						break;
					case 16: // all-time-download
						io::write_uint32(s.all_time_download, ptr);
						break;
					case 17: // unchoked-peers
						io::write_uint32(s.num_uploads, ptr);
						break;
					case 18: // num-connections
						io::write_uint32(s.num_connections, ptr);
						break;
					case 19: // queue-position
						io::write_uint32(s.queue_position, ptr);
						break;
					case 20: // state
					{
						int state;
						switch (s.state)
						{
#ifndef TORRENT_NO_DEPRECATE
							case torrent_status::queued_for_checking:
#endif
							case torrent_status::checking_files:
							case torrent_status::allocating:
							case torrent_status::checking_resume_data:
								state = 0; // checking-files
								break;
							case torrent_status::downloading_metadata:
								state = 1; // downloading-metadata
								break;
							case torrent_status::downloading:
							default:
								state = 2; // downloading
								break;
							case torrent_status::finished:
							case torrent_status::seeding:
								state = 3; // seeding
								break;
						};
						io::write_uint8(state, ptr);
						break;
					}
					case 21: // failed-bytes
						io::write_uint64(s.total_failed_bytes, ptr);
						break;
					case 22: // redundant-bytes
						io::write_uint64(s.total_redundant_bytes, ptr);
						break;
					default:
					TORRENT_ASSERT(false);
				}
			}
		}

		// now that we know how many torrents we wrote, fill in the
		// counter
		char* ptr2 = &response[num_torrents_pos];
		io::write_uint32(num_torrents, ptr2);

		// send list of removed torrents
		for (std::vector<sha1_hash>::iterator i = removed_torrents.begin()
			, end(removed_torrents.end()); i != end; ++i)
		{
			std::copy(i->begin(), i->end(), ptr);
		}

		return send_packet(st->conn, 0x2, &response[0], response.size());
	}

	int libtorrent_webui::parse_torrent_args(std::vector<torrent_handle>& torrents, conn_state* st)
	{
		char* ptr = st->data;
		int num_torrents = io::read_uint16(ptr);

		// there are only supposed to be one ore more info-hashes as arguments. Each info-hash is
		// in its binary representation, and hence 20 bytes long.
		if ((st->len < num_torrents * 20))
			return invalid_argument_type;

		for (int i = 0; i < num_torrents; ++i)
		{
			sha1_hash h;
			memcpy(&h[0], &ptr[i*20], 20);

			// TODO: this call is blocking. Instead, use the torrent_history object for this lookup
			// or make this function fast in libtorrent
			torrent_handle th = m_ses.find_torrent(h);
			if (!th.is_valid()) continue;
			torrents.push_back(th);
		}
		return no_error;
	}

#define TORRENT_APPLY_FUN \
		std::vector<torrent_handle> torrents; \
		int ret = parse_torrent_args(torrents, st); \
		if (ret != no_error) return error(st, ret); \
		\
		for (std::vector<torrent_handle>::iterator i = torrents.begin() \
			, end(torrents.end()); i != end; ++i)

	bool libtorrent_webui::start(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->auto_managed(true);
			i->clear_error();
			i->resume();
		}
		return respond(st, 0, torrents.size());
	}

	bool libtorrent_webui::stop(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->auto_managed(false);
			i->pause();
		}
		return respond(st, 0, torrents.size());
	}

	bool libtorrent_webui::set_auto_managed(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->auto_managed(true);
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::clear_auto_managed(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->auto_managed(false);
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::queue_up(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->queue_position_up();
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::queue_down(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->queue_position_down();
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::queue_top(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->queue_position_top();
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::queue_bottom(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->queue_position_bottom();
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::remove(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			m_ses.remove_torrent(*i);
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::remove_and_data(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			m_ses.remove_torrent(*i, session::delete_files);
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::force_recheck(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->force_recheck();
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::set_sequential_download(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->set_sequential_download(true);
		}
		return respond(st, 0, torrents.size());
	}
	bool libtorrent_webui::clear_sequential_download(conn_state* st)
	{
		TORRENT_APPLY_FUN
		{
			i->set_sequential_download(false);
		}
		return respond(st, 0, torrents.size());
	}

#undef TORRENT_APPLY_FUN

	bool libtorrent_webui::list_settings(conn_state* st)
	{
		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(no_error, ptr);

		io::write_uint32(settings_pack::num_string_settings, ptr);
		io::write_uint32(settings_pack::num_int_settings, ptr);
		io::write_uint32(settings_pack::num_bool_settings, ptr);

		for (int i = settings_pack::string_type_base;
			i < settings_pack::max_string_setting_internal; ++i)
		{
			char const* n = name_for_setting(i);
			int len = strlen(n);
			TORRENT_ASSERT(len < 256);
			io::write_uint8(len, ptr);
			std::copy(n, n + len, ptr);
			TORRENT_ASSERT(i < 65536);
			io::write_uint16(i, ptr);
		}

		for (int i = settings_pack::int_type_base;
			i < settings_pack::max_int_setting_internal; ++i)
		{
			char const* n = name_for_setting(i);
			int len = strlen(n);
			TORRENT_ASSERT(len < 256);
			io::write_uint8(len, ptr);
			std::copy(n, n + len, ptr);
			TORRENT_ASSERT(i < 65536);
			io::write_uint16(i, ptr);
		}

		for (int i = settings_pack::bool_type_base;
			i < settings_pack::max_bool_setting_internal; ++i)
		{
			char const* n = name_for_setting(i);
			int len = strlen(n);
			TORRENT_ASSERT(len < 256);
			io::write_uint8(len, ptr);
			std::copy(n, n + len, ptr);
			TORRENT_ASSERT(i < 65536);
			io::write_uint16(i, ptr);
		}
		return send_packet(st->conn, 0x2, &response[0], response.size());
	}

	bool libtorrent_webui::set_settings(conn_state* st)
	{
		char* ptr = st->data;
		if (st->len < 2) return error(st, invalid_number_of_args);

		int num_settings = io::read_uint16(ptr);
		st->len -= 2;

		settings_pack pack;

		for (int i = 0; i < num_settings; ++i)
		{
			if (st->len < 2) return error(st, invalid_number_of_args);
			int sett = io::read_uint16(ptr);
			st->len -= 2;

			if (sett >= settings_pack::string_type_base && sett < settings_pack::max_string_setting_internal)
			{
				if (st->len < 2) return error(st, invalid_number_of_args);
				int len = io::read_uint16(ptr);
				st->len -= 2;
				std::string str;
				str.resize(len);
				if (st->len < len) return error(st, invalid_number_of_args);
				std::copy(ptr, ptr + len, str.begin());
				ptr += len;
			}
			else if (sett >= settings_pack::int_type_base && sett < settings_pack::max_int_setting_internal)
			{
				if (st->len < 4) return error(st, invalid_number_of_args);
				pack.set_int(sett, io::read_uint32(ptr));
				st->len -= 4;
			}
			else if (sett >= settings_pack::bool_type_base && sett < settings_pack::max_bool_setting_internal)
			{
				if (st->len < 1) return error(st, invalid_number_of_args);
				pack.set_bool(sett, io::read_uint8(ptr));
				st->len -= 1;
			}
			else
			{
				return error(st, invalid_argument);
			}
		}

		m_ses.apply_settings(pack);

		return error(st, no_error);
	}

	bool libtorrent_webui::get_settings(conn_state* st)
	{
		char* iptr = st->data;
		if (st->len < 2) return error(st, invalid_number_of_args);
		int num_settings = io::read_uint16(iptr);
		st->len -= 2;

		if (st->len < num_settings * 2) return invalid_argument_type;

		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(no_error, ptr);

		io::write_uint16(num_settings, ptr);

		aux::session_settings s = m_ses.get_settings();

		for (int i = 0; i < num_settings; ++i)
		{
			int sett = io::read_uint16(iptr);
			if (sett >= settings_pack::string_type_base && sett < settings_pack::max_string_setting_internal)
			{
				std::string const& v = s.get_str(sett);
				io::write_uint16(v.length(), ptr);
				std::copy(v.begin(), v.end(), ptr);
			}
			else if (sett >= settings_pack::int_type_base && sett < settings_pack::max_int_setting_internal)
			{
				io::write_uint32(s.get_int(sett), ptr);
			}
			else if (sett >= settings_pack::bool_type_base && sett < settings_pack::max_bool_setting_internal)
			{
				io::write_uint8(s.get_bool(sett), ptr);
			}
			else
			{
				return error(st, invalid_argument);
			}
		}

		return send_packet(st->conn, 0x2, &response[0], response.size());
	}

	bool libtorrent_webui::list_stats(conn_state* st)
	{
		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(no_error, ptr);

		std::vector<stats_metric> stats = session_stats_metrics();
		io::write_uint32(stats.size(), ptr);

		for (std::vector<stats_metric>::iterator i = stats.begin()
			, end(stats.end()); i != end; ++i)
		{
			io::write_uint8(i->type, ptr);
			int len = strlen(i->name);
			TORRENT_ASSERT(len < 256);
			io::write_uint8(len, ptr);
			std::copy(i->name, i->name + len, ptr);
		}

		return send_packet(st->conn, 0x2, &response[0], response.size());
	}

	bool libtorrent_webui::get_stats(conn_state* st)
	{
		std::vector<char> response;
		std::back_insert_iterator<std::vector<char> > ptr(response);

		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(no_error, ptr);

		m_ses.post_session_stats();
		std::auto_ptr<alert> a = wait_for_alert(*m_alert, session_stats_alert::alert_type);
		session_stats_alert* ss = alert_cast<session_stats_alert>(a.get());

		TORRENT_ASSERT(ss);

		std::vector<boost::uint64_t> const& stats = ss->values;

		io::write_uint32(stats.size(), ptr);

		for (std::vector<boost::uint64_t>::const_iterator i = stats.begin()
			, end(stats.end()); i != end; ++i)
		{
			io::write_uint64(*i, ptr);
		}

		return send_packet(st->conn, 0x2, &response[0], response.size());
	}

	char const* fun_name(int function_id)
	{
		if (function_id < 0 || function_id >= sizeof(functions)/sizeof(functions[0]))
		{
			return "unknown function";
		}

		return functions[function_id].name;
	}

	bool libtorrent_webui::handle_websocket_data(mg_connection* conn
		, int bits, char* data, size_t length)
	{
		// TODO: this should really be handled at one layer below
		// ping
		if ((bits & 0xf) == 0x9)
		{
			// send pong
			fprintf(stderr, "PING\n");
			return send_packet(conn, 0xa, NULL, 0);
		}

		// only support binary, non-fragmented frames
		if ((bits & 0xf) != 0x2)
		{
			fprintf(stderr, "ERROR: received packet that's not in binary mode\n");
			return false;
		}

		// parse RPC message

		// RPC call is always at least 3 bytes.
		if (length < 3)
		{
			fprintf(stderr, "ERROR: received packet that's smaller than 3 bytes (%d)\n", int(length));
			return false;
		}

		conn_state st;
		st.conn = conn;

		st.data = data;
		st.function_id = io::read_uint8(st.data);
		st.transaction_id = io::read_uint16(st.data);

		if (st.function_id & 0x80)
		{
			// RPC responses is at least 4 bytes
			if (length < 4)
			{
				fprintf(stderr, "ERROR: received RPC response that's smaller than 4 bytes (%d)\n", int(length));
				return false;
			}
			int status = io::read_uint8(st.data);
			// this is a response to a function call
			fprintf(stderr, "RETURNED: %s (status: %d)\n", fun_name(st.function_id & 0x7f), status);
		}
		else
		{
			st.len = data + length - st.data;
			// TOOD: parse this out of the request_info
			st.perms = NULL;

			fprintf(stderr, "CALL: %s (%d bytes arguments)\n", fun_name(st.function_id), st.len);
			if (st.function_id >= 0 && st.function_id < sizeof(functions)/sizeof(functions[0]))
			{
				return (this->*functions[st.function_id].handler)(&st);
			}
			else
			{
				return error(&st, no_such_function);
			}
		}
		return true;
	}

	bool libtorrent_webui::respond(conn_state* st, int error, int val)
	{
		char rpc[6];
		char* ptr = rpc;

		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(no_error, ptr);
		io::write_uint16(val, ptr);

		return send_packet(st->conn, 0x2, rpc, 8);
	}

	bool libtorrent_webui::error(conn_state* st, int error)
	{
		char rpc[4];
		char* ptr = &rpc[0];
		io::write_uint8(st->function_id | 0x80, ptr);
		io::write_uint16(st->transaction_id, ptr);
		io::write_uint8(error, ptr);

		return send_packet(st->conn, 0x2, rpc, 4);
	}

	bool libtorrent_webui::call_rpc(mg_connection* conn, int function, char const* data, int len)
	{
		buffer buf(len + 3);
		char* ptr = &buf[0];
		TORRENT_ASSERT(function >= 0 && function < 128);

		// function id
		io::write_uint8(function, ptr);

		// transaction id
		boost::uint16_t tid = m_transaction_id++;
		io::write_uint16(tid, ptr);

		if (len > 0) memcpy(ptr, data, len);

		return send_packet(conn, 0x2, &buf[0], buf.size());
	}

}

