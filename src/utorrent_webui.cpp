/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include "utorrent_webui.hpp"
#include "disk_space.hpp"
#include "base64.hpp"

#include <string.h> // for strcmp() 
#include <stdio.h>
#include <vector>
#include <map>
#include <boost/intrusive_ptr.hpp>
#include <boost/cstdint.hpp>

extern "C" {
#include "mongoose.h"
}

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/socket_io.hpp" // for print_address
#include "libtorrent/io.hpp" // for read_int32
#include "libtorrent/magnet_uri.hpp" // for make_magnet_uri
#include "libtorrent/escape_string.hpp" // for unescape_string
#include "response_buffer.hpp" // for appendf
#include "torrent_post.hpp"
#include "escape_json.hpp"
#include "auto_load.hpp"
#include "save_settings.hpp"
#include "torrent_history.hpp"

namespace libtorrent
{

utorrent_webui::utorrent_webui(session& s, save_settings_interface* sett
		, auto_load* al, torrent_history* hist)
	: m_ses(s)
	, m_settings(sett)
	, m_al(al)
	, m_hist(hist)
{
	m_start_time = time(NULL);
	m_version = 1;

	boost::uint64_t seed = total_microseconds(time_now_hires() - min_time());
	m_token = to_hex(hasher((char const*)&seed, sizeof(seed)).final().to_string());

	m_params_model.save_path = ".";
	m_webui_cookie = "{}";

	if (m_settings)
	{
		m_params_model.save_path = m_settings->get_str("save_path", ".");
		m_webui_cookie = m_settings->get_str("ut_webui_cookie", "{}");
		int port = m_settings->get_int("listen_port", -1);
		if (port != -1)
		{
			error_code ec;
			m_ses.listen_on(std::make_pair(port, port+1), ec);
		}
	}
}

utorrent_webui::~utorrent_webui() {}

struct method_handler
{
	char const* action_name;
	void (utorrent_webui::*fun)(std::vector<char>&, char const* args);
};

static method_handler handlers[] =
{
	{ "start", &utorrent_webui::start },
	{ "forcestart", &utorrent_webui::force_start },
	{ "stop", &utorrent_webui::stop },
	{ "pause", &utorrent_webui::stop},
	{ "unpause", &utorrent_webui::start},

	{ "queueup", &utorrent_webui::queue_up },
	{ "queuedown", &utorrent_webui::queue_down },
	{ "queuetop", &utorrent_webui::queue_top },
	{ "queuebottom", &utorrent_webui::queue_bottom },

	{ "getfiles", &utorrent_webui::send_file_list },
	{ "getpeers", &utorrent_webui::send_peer_list },
	{ "getprops", &utorrent_webui::get_properties },
	{ "recheck", &utorrent_webui::recheck },
	{ "remove", &utorrent_webui::remove_torrent },
//	{ "setprio", &utorrent_webui:: },
	{ "getsettings", &utorrent_webui::get_settings },
	{ "setsetting", &utorrent_webui::set_settings },
	{ "add-url", &utorrent_webui::add_url },
//	{ "setprops", &utorrent_webui:: },
	{ "removedata", &utorrent_webui::remove_torrent_and_data },
//	{ "list-dirs", &utorrent_webui:: },
//	{ "rss-update", &utorrent_webui:: },
//	{ "rss-remove", &utorrent_webui:: },
//	{ "filter-update", &utorrent_webui:: },
//	{ "filter-remove", &utorrent_webui:: },
	{ "removetorrent", &utorrent_webui::remove_torrent },
	{ "removedatatorrent", &utorrent_webui::remove_torrent_and_data },
//	{ "getversion", &utorrent_webui:: },
//	{ "add-peer", &utorrent_webui:: },
};

void return_error(mg_connection* conn)
{
	mg_printf(conn, "HTTP/1.1 400 Invalid Request\r\n"
		"Content: close\r\n\r\n");
}

bool utorrent_webui::handle_http(mg_connection* conn, mg_request_info const* request_info)
{
	// redirect to /gui/
	if (strcmp(request_info->uri, "/gui") == 0
		|| (strcmp(request_info->uri, "/gui/") == 0
			&& request_info->query_string == NULL))
	{
		mg_printf(conn, "HTTP/1.1 301 Moved Permanently\r\n"
			"Content: close\r\n"
			"Location: /gui/index.html\r\n\r\n");
		return true;
	}

	std::vector<char> response;

	// Auth token handling
	if (strcmp(request_info->uri, "/gui/token.html") == 0)
	{
		appendf(response, "<html><div id=\"token\" style=\"display:none;\">%s</div></html>"
			, m_token.c_str());
		response.push_back('\0');

		mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			"Conten-Length: %d\r\n"
			"Content-Type: text/html\r\n\r\n"
			"%s", int(response.size()-1), &response[0]);
		return true;
	}

	// all other requests use the path /gui/ with varying query strings
	if (strcmp(request_info->uri, "/gui/") != 0) return false;

	if (request_info->query_string == NULL)
	{
		mg_printf(conn, "HTTP/1.1 400 Invalid Request (no query string)\r\n"
			"Connection: close\r\n\r\n");
		return true;
	}

//	printf("%s%s%s\n", request_info->uri
//		, request_info->query_string ? "?" : ""
//		, request_info->query_string ? request_info->query_string : "");

	// TODO: make this configurable
	int ret = 0;
/*
	char token[50];
	// first, verify the token
	ret = mg_get_var(request_info->query_string, strlen(request_info->query_string)
		, "token", token, sizeof(token));

	if (ret <= 0 || m_token != token)
	{
		mg_printf(conn, "HTTP/1.1 400 Invalid Request (invalid token)\r\n"
			"Connection: close\r\n\r\n");
		return true;
	}
*/

	appendf(response, "{\"build\":%d", LIBTORRENT_VERSION_NUM);

	char action[50];
	// then, find the action
	ret = mg_get_var(request_info->query_string, strlen(request_info->query_string)
		, "action", action, sizeof(action));
	if (ret > 0)
	{
		// add-file is special, since it posts the torrent
		if (strcmp(action, "add-file") == 0)
		{
			add_torrent_params p = m_params_model;
			error_code ec;
			if (!parse_torrent_post(conn, p, ec))
			{
				mg_printf(conn, "HTTP/1.1 400 Invalid Request (%s)\r\n"
					"Connection: close\r\n\r\n", ec.message().c_str());
				return true;
			}

			m_ses.async_add_torrent(p);
		}
		else
		{
			for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i)
			{
				if (strcmp(action, handlers[i].action_name)) continue;

				(this->*handlers[i].fun)(response, request_info->query_string);
				break;
			}
		}
	}

	char buf[10];
	if (mg_get_var(request_info->query_string, strlen(request_info->query_string)
		, "list", buf, sizeof(buf)) > 0
		&& atoi(buf) > 0)
	{
		send_torrent_list(response, request_info->query_string);
	}

	// we need a null terminator
	appendf(response, "}");
	response.push_back('\0');

	// subtract one from content-length
	// to not count null terminator
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
		"Content-Type: text/json\r\n"
		"Content-Length: %d\r\n\r\n", int(response.size()) - 1);
	mg_write(conn, &response[0], response.size()-1);
	printf("%s\n", &response[0]);
	return true;
}

void utorrent_webui::start(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->auto_managed(true);
		i->resume();
	}
}

void utorrent_webui::stop(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->auto_managed(false);
		i->pause();
	}
}

void utorrent_webui::force_start(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->auto_managed(false);
		i->resume();
	}
}

void utorrent_webui::recheck(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->force_recheck();
	}
}

void utorrent_webui::queue_up(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->queue_position_up();
	}
}

void utorrent_webui::queue_down(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->queue_position_down();
	}
}

void utorrent_webui::queue_top(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->queue_position_top();
	}
}

void utorrent_webui::queue_bottom(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->queue_position_bottom();
	}
}

void utorrent_webui::remove_torrent(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		m_ses.remove_torrent(*i);
	}
}

void utorrent_webui::remove_torrent_and_data(std::vector<char>&, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		m_ses.remove_torrent(*i, session::delete_files);
	}
}

char const* settings_name(int s)
{
	return name_for_setting(s);
}

void utorrent_webui::get_settings(std::vector<char>& response, char const* args)
{
	appendf(response, ", \"settings\": [");

	libtorrent::aux::session_settings sett = m_ses.get_settings();

	// type: 0 = int, 1= bool, 2=string
	int first = 1;
	for (int i = 0; i < settings_pack::num_string_settings; ++i)
	{
		int s = settings_pack::string_type_base + i;
		appendf(response, ",[\"%s\",2,\"%s\",{\"access\":\"Y\"}]\n" + first
			, settings_name(s), escape_json(sett.get_str(s)).c_str());
		first = 0;
	}

	for (int i = 0; i < settings_pack::num_bool_settings; ++i)
	{
		int s = settings_pack::bool_type_base + i;
		char const* sname;
		bool value;
		if (s == settings_pack::use_read_cache)
		{
			sname = "cache.read";
			value = sett.get_bool(s);
		}
		else
		{
			sname = settings_name(s);
			value = sett.get_bool(s);
		}
		appendf(response, ",[\"%s\",1,\"%s\",{\"access\":\"Y\"}]\n" + first
			, sname, value ? "true" : "false");
		first = 0;
	}

	for (int i = 0; i < settings_pack::num_int_settings; ++i)
	{
		int s = settings_pack::int_type_base + i;
		char const* sname;
		boost::int64_t value;
		if (s == settings_pack::cache_size)
		{
			sname = "cache.override_size";
			value = boost::int64_t(sett.get_int(s)) * 16 / 1024;
		}
		else
		{
			sname = settings_name(s);
			value = sett.get_int(s);
		}

		appendf(response, ",[\"%s\",0,\"%"PRId64"\",{\"access\":\"Y\"}]\n" + first
			, sname, value);
		first = 0;
	}

	if (m_al)
	{
		appendf(response,
			",[\"dir_autoload\",2,\"%s\",{\"access\":\"Y\"}]\n"
			",[\"dir_autoload_flag\",1,\"%s\",{\"access\":\"Y\"}]" + first
			, escape_json(m_al->auto_load_dir()).c_str()
			, m_al->scan_interval() ? "true" : "false");
		first = 0;
	}

	appendf(response,
		",[\"dir_active_download\",2,\"%s\",{\"access\":\"Y\"}]\n"
		",[\"bind_port\",0,\"%d\",{\"access\":\"Y\"}]\n"
		",[\"bt.transp_disposition\",0,\"%d\",{\"access\":\"Y\"}]\n"
		",[\"webui.cookie\",2,\"%s\",{\"access\":\"Y\"}]\n"
		",[\"language\",0,\"0\",{\"access\":\"Y\"}]\n"
		",[\"webui.enable_listen\",1,\"true\",{\"access\":\"Y\"}]\n"
		",[\"webui.enable_guest\",1,\"false\",{\"access\":\"Y\"}]\n"
		",[\"webui.port\",0,\"8080\",{\"access\":\"Y\"}]\n"
		",[\"cache.override\",1,\"true\",{\"access\":\"Y\"}]\n"
		// the webUI uses the existence of this setting as an
		// indication of supporting the getpeers action, so we
		// need to define it in order to support peers
		",[\"webui.uconnect_enable\",1,\"false\",{\"access\":\"Y\"}]\n"
		"]"
		 + first
		, escape_json(m_params_model.save_path).c_str()
		, m_ses.listen_port()
		, (sett.get_bool(settings_pack::enable_outgoing_utp) ? 1 : 0)
			+ (sett.get_bool(settings_pack::enable_incoming_utp) ? 2 : 0)
			+ (sett.get_bool(settings_pack::enable_outgoing_tcp) ? 4 : 0)
			+ (sett.get_bool(settings_pack::enable_incoming_tcp) ? 8 : 0)
		, escape_json(m_webui_cookie).c_str());
}

void utorrent_webui::set_settings(std::vector<char>& response, char const* args)
{
	settings_pack pack;

	for (char const* s = strstr(args, "&s="); s; s = strstr(s, "&s="))
	{
		s += 3;
		char const* key_end = strchr(s, '&');
		if (key_end == NULL) continue;
		if (strncmp(key_end, "&v=", 3) != 0) continue;
		char const* v_end = strchr(key_end + 3, '&');
		if (v_end == NULL) v_end = s + strlen(s);

		std::string key(s, key_end - s);
		std::string value(key_end + 3, v_end - key_end - 3);
		error_code ec;
		value = unescape_string(value, ec);

		s = v_end;

		if (ec) continue;

		if (key == "webui.cookie")
		{
			m_webui_cookie = value;
			if (m_settings) m_settings->set_str("ut_webui_cookie", value);
		}
		else if (key == "bind_port")
		{
			int port = atoi(value.c_str());
			error_code ec;
			m_ses.listen_on(std::make_pair(port, port+1), ec);
			if (m_settings) m_settings->set_int("listen_port", port);
		}
		else if (key == "bt.transp_disposition")
		{
			int mask = atoi(value.c_str());
			pack.set_bool(settings_pack::enable_outgoing_utp, mask & 1);
			pack.set_bool(settings_pack::enable_incoming_utp, mask & 2);
			pack.set_bool(settings_pack::enable_outgoing_tcp, mask & 4);
			pack.set_bool(settings_pack::enable_incoming_tcp, mask & 8);
		}
		else if (key == "dir_autoload" && m_al)
		{
			m_al->set_auto_load_dir(value);
		}
		else if (key == "dir_autoload_flag" && m_al)
		{
			m_al->set_scan_interval(value == "false" ? 0 : 20);
		}
		else if (key == "dir_active_download")
		{
			m_params_model.save_path = value;
			if (m_al)
			{
				add_torrent_params p = m_al->params_model();
				p.save_path = value;
				m_al->set_params_model(p);
			}
			if (m_settings) m_settings->set_str("save_path", value);
		}
		else if (key == "cache.override_size")
		{
			int size = atoi(value.c_str()) * 1024 / 16;
			pack.set_int(settings_pack::cache_size, size);
		}
		else
		{
			int field = setting_by_name(key.c_str());
			if (field < 0)
			{
				fprintf(stderr, "unknown setting: %s\n", key.c_str());
				continue;
			}
			switch (field & settings_pack::type_mask)
			{
				case settings_pack::string_type_base:
					pack.set_str(field, value.c_str());
					break;
				case settings_pack::int_type_base:
					pack.set_int(field, atoi(value.c_str()));
					break;
				case settings_pack::bool_type_base:
					pack.set_bool(field, value == "true");
					break;
			}
		}
	}
	m_ses.apply_settings(pack);

	error_code ec;
	if (m_settings) m_settings->save(ec);
}

void utorrent_webui::send_file_list(std::vector<char>& response, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	appendf(response, ",\"files\":[");
	int first = 1;
	std::vector<boost::int64_t> progress;
	std::vector<int> file_prio;
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->file_progress(progress);
		file_prio = i->file_priorities();
		boost::intrusive_ptr<torrent_info> ti = i->torrent_file();
		if (!ti) continue;
		file_storage const& files = ti->files();

		appendf(response, ",\"%s\",["+first, to_hex(ti->info_hash().to_string()).c_str());
		int first_file = 1;
		for (int i = 0; i < files.num_files(); ++i)
		{
			int first_piece = files.file_offset(i) / files.piece_length();
			int last_piece = (files.file_offset(i) + files.file_size(i)) / files.piece_length();
			appendf(response, ",[\"%s\", %"PRId64", %"PRId64", %d" + first_file
				, escape_json(files.file_name(i)).c_str()
				, files.file_size(i)
				, progress[i]
				, (file_prio[i]+3) / 4 // uTorrent's web UI uses 4 priority levels
				);

			if (m_version > 0)
			{
				appendf(response, ", %d, %d]"
					, first_piece
					, last_piece - first_piece);
			}
			else
			{
				response.push_back(']');
			}
			first_file = 0;
		}

		response.push_back(']');
		first = 0;
	}
	response.push_back(']');
}

std::string trackers_as_string(torrent_handle h)
{
	std::string ret;
	std::vector<announce_entry> trackers = h.trackers();
	int last_tier = 0;
	for (std::vector<announce_entry>::iterator i = trackers.begin()
		, end(trackers.end()); i != end; ++i)
	{
		if (last_tier != i->tier) ret += "\\r\\n";
		last_tier = i->tier;
		ret += i->url;
		ret += "\\r\\n";
	}
	return ret;
}

void utorrent_webui::add_url(std::vector<char>&, char const* args)
{
	char url[4096];
	if (mg_get_var(args, strlen(args)
		, "url", url, sizeof(url)) <= 0)
	{
		if (mg_get_var(args, strlen(args)
			, "s", url, sizeof(url)) <= 0)
		{
			return;
		}
	}

	add_torrent_params p = m_params_model;
	p.url = url;

	m_ses.async_add_torrent(p);
}

void utorrent_webui::get_properties(std::vector<char>& response, char const* args)
{
	std::vector<torrent_handle> t = parse_torrents(args);
	appendf(response, ",\"props\":[");
	int first = 1;
	for (std::vector<torrent_handle>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		torrent_status st = i->status();
		boost::intrusive_ptr<torrent_info> ti = i->torrent_file();
		appendf(response, ",{\"hash\":\"%s\","
			"\"trackers\":\"%s\","
			"\"ulrate\":%d,"
			"\"dlrate\":%d,"
			"\"superseed\":%d,"
			"\"dht\":%d,"
			"\"pex\":%d,"
			"\"seed_override\":%d,"
			"\"seed_ratio\": %f,"
			"\"seed_time\": %d,"
			"\"ulslots\": %d,"
			"\"seed_num\": %d}" + first
			, ti ? to_hex(ti->info_hash().to_string()).c_str() : ""
			, trackers_as_string(*i).c_str()
			, i->download_limit()
			, i->upload_limit()
			, st.super_seeding
			, ti && ti->priv() ? 0 : m_ses.is_dht_running()
			, ti && ti->priv() ? 0 : 1
			, 0
			, 0
			, 0
			, 0
			, 0
			);
		first = 0;
	}
	response.push_back(']');
}

std::string utorrent_peer_flags(peer_info const& pi)
{
	std::string ret;
	if (pi.flags & peer_info::remote_interested)
	{
		ret += (pi.flags & peer_info::remote_choked) ? 'u' : 'U';
	}

	if (pi.flags & peer_info::interesting)
	{
		ret += (pi.flags & peer_info::choked) ? 'd' : 'D';
	}

	if (pi.flags & peer_info::optimistic_unchoke)
		ret += 'O';

	if (pi.flags & peer_info::snubbed)
		ret += 'S';

	if ((pi.flags & peer_info::local_connection))
		ret += 'I';

	if ((pi.source & peer_info::dht))
		ret += 'H';

	if ((pi.source & peer_info::pex))
		ret += 'X';

	if ((pi.source & peer_info::lsd))
		ret += 'L';

	if ((pi.flags & peer_info::rc4_encrypted))
		ret += 'E';
	else if ((pi.flags & peer_info::plaintext_encrypted))
		ret += 'e';

	if ((pi.flags & peer_info::on_parole))
		ret += 'F';

	if (pi.connection_type == peer_info::bittorrent_utp)
		ret += 'P';
	return ret;
}

void utorrent_webui::send_peer_list(std::vector<char>& response, char const* args)
{
	std::vector<torrent_handle> torrents = parse_torrents(args);
	appendf(response, ",\"peers\":[");
	int first = 1;
	for (std::vector<torrent_handle>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{
		boost::intrusive_ptr<torrent_info> ti = i->torrent_file();
		if (!ti) continue;

		appendf(response, ",\"%s\",[" + first
			, to_hex(i->info_hash().to_string()).c_str());

		int first_peer = 1;
		std::vector<peer_info> peers;
		i->get_peer_info(peers);
		for (std::vector<peer_info>::iterator p = peers.begin()
			, pend(peers.end()); p != pend; ++p)
		{
			appendf(response, ",[\"%c%c\",\"%s\",\"%s\",%d,%d,\"%s\",\"%s\",%d,%d,%d,%d,%d"
				",%d,%"PRId64",%"PRId64",%d,%d,%d,%d,%d,%d,%d]" + first_peer
				, isprint(p->country[0]) ? p->country[0] : ' '
				, isprint(p->country[1]) ? p->country[1] : ' '
				, print_endpoint(p->ip).c_str()
				, ""
				, p->connection_type == peer_info::bittorrent_utp
				, p->ip.port()
				, escape_json(p->client).c_str()
				, utorrent_peer_flags(*p).c_str()
				, p->num_pieces * 1000 / ti->num_pieces()
				, p->down_speed
				, p->up_speed
				, p->download_queue_length
				, p->upload_queue_length
				, total_seconds(p->last_request)
				, p->total_upload
				, p->total_download
				, p->num_hashfails
				, 0
				, 0
				, 0
				, p->send_buffer_size
				, total_seconds(p->last_active)
				, 0
				);
			first_peer = 0;
		}

		response.push_back(']');
		first = 0;
	}
	response.push_back(']');
}

enum ut_state_t
{
	STARTED = 1,
	CHECKING = 2,
	START_AFTER_CHECK = 4,
	CHECKED = 8,
	ERROR = 16,
	PAUSED = 32,
	AUTO = 64,
	LOADED = 128
};

int utorrent_status(torrent_status const& st)
{
	int ret = 0;
	if (st.has_metadata) ret |= LOADED;
	if (!st.paused) ret |= STARTED;
	if (st.state == torrent_status::queued_for_checking || st.state == torrent_status::checking_files)
		ret |= CHECKING;
	else
		ret |= CHECKED;
	if (!st.error.empty()) ret |= ERROR;
	if (st.auto_managed) ret |= AUTO;
	return ret;
}

std::string utorrent_message(torrent_status const& st)
{
	if (!st.error.empty()) return "Error: " + st.error;

	if (st.state == torrent_status::queued_for_checking
		|| st.state == torrent_status::checking_resume_data)
		return "Checking";

	if (st.state == torrent_status::checking_files)
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "Checking (%d.%1d%%)"
			, st.progress_ppm / 10000, st.progress_ppm % 10000);
		return msg;
	}
	if (st.state == torrent_status::downloading)
	{
		if (st.auto_managed)
		{
			return st.paused ? "Queued" : "Downloading";
		}
		else
		{
			return st.paused ? "Stopped" : "[F] Downloading";
		}
	}

	if (st.state == torrent_status::seeding
		|| st.state == torrent_status::finished)
	{
		if (st.auto_managed)
		{
			return st.paused ? "Queued Seed" : "Seeding";
		}
		else
		{
			return st.paused ? "Finished" : "[F] Seeding";
		}
	}

	if (st.state == torrent_status::downloading_metadata)
		return "Downloading metadata";

	if (st.state == torrent_status::allocating)
		return "Allocating";

	assert(false);
	return "??";
}

void utorrent_webui::send_torrent_list(std::vector<char>& response, char const* args)
{
	int cid = 0;
	char buf[50];
	// first, find the action
	int ret = mg_get_var(args, strlen(args)
		, "cid", buf, sizeof(buf));
	if (ret > 0) cid = atoi(buf);

	appendf(response, cid > 0 ? ",\"torrentp\":[" : ",\"torrents\":[");

	std::vector<torrent_status> torrents;
	m_hist->updated_since(cid, torrents);

	int first = 1;
	for (std::vector<torrent_status>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{
		boost::intrusive_ptr<torrent_info> ti = i->handle.torrent_file();
		appendf(response, ",[\"%s\",%d,\"%s\",%"PRId64",%d,%"PRId64",%"PRId64",%f,%d,%d,%d,\"%s\",%d,%d,%d,%d,%d,%d,%"PRId64"" + first
			, to_hex(i->handle.info_hash().to_string()).c_str()
			, utorrent_status(*i)
			, escape_json(i->handle.name()).c_str()
			, ti ? ti->total_size() : 0
			, i->progress_ppm / 1000
			, i->total_payload_download
			, i->total_payload_upload
			, i->all_time_download == 0 ? 0 : float(i->all_time_upload) / i->all_time_download
			, i->upload_rate
			, i->download_rate
			, i->download_rate == 0 ? 0 : (i->total_wanted - i->total_wanted_done) / i->download_rate
			, "" // label
			, i->num_peers - i->num_seeds
			, i->list_peers - i->list_seeds
			, i->num_seeds
			, i->list_seeds
			, i->distributed_full_copies < 0 ? 0
				: int(i->distributed_full_copies << 16) + int(i->distributed_fraction * 65536 / 1000)
			, i->queue_position
			, i->total_wanted - i->total_wanted_done
			);

		if (m_version > 0)
		{
			appendf(response, ",\"%s\",\"%s\",\"%s\",\"%s\",%"PRId64",%"PRId64",\"%s\",\"%s\",%d,\"%s\"]"
			, "" // url this torrent came from
			, "" // feed URL this torrent belongs to
			, utorrent_message(*i).c_str()
			, to_hex(i->handle.info_hash().to_string()).c_str()
			, i->added_time
			, i->completed_time
			, "" // app
			, escape_json(i->handle.save_path()).c_str()
			, 0
			, "");
		}
		else
		{
			response.push_back(']');
		}
		first = 0;
	}

	std::vector<sha1_hash> removed;
	m_hist->removed_since(cid, removed);

	appendf(response, "], \"torrentm\": [");
	first = 1;
	for (std::vector<sha1_hash>::iterator i = removed.begin()
		, end(removed.end()); i != end; ++i)
	{
		appendf(response, ",\"%s\"" + first, to_hex(i->to_string()).c_str());
		first = 0;
	}
	// TODO: support labels
	appendf(response, "], \"label\": [], \"torrentc\": \"%d\"", m_hist->frame());
}

std::vector<torrent_handle> utorrent_webui::parse_torrents(char const* args) const
{
	std::vector<torrent_handle> ret;

	for (char const* hash = strstr(args, "&hash="); hash; hash = strstr(hash, "&hash="))
	{
		hash += 6;
		char const* end = strchr(hash, '&');
		if (end != NULL && end - hash != 40) continue;
		if (end == NULL && strlen(hash) != 40) continue;
		sha1_hash h;
		bool ok = from_hex(hash, 40, (char*)&h[0]);
		if (!ok) continue;
		torrent_handle th = m_ses.find_torrent(h);
		if (!th.is_valid()) continue;
		ret.push_back(th);
	}
	return ret;
}

}

