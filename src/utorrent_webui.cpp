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

#include "utorrent_webui.hpp"
#include "disk_space.hpp"
#include "base64.hpp"
#include "auth.hpp"
#include "no_auth.hpp"
#include "disk_space.hpp"

#include <string.h> // for strcmp() 
#include <stdio.h>
#include <vector>
#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/cstdint.hpp>

extern "C" {
#include "local_mongoose.h"
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
#include "libtorrent/string_util.hpp" // for string_begins_no_case
#include "response_buffer.hpp" // for appendf
#include "torrent_post.hpp"
#include "escape_json.hpp"
#include "auto_load.hpp"
#include "save_settings.hpp"
#include "torrent_history.hpp"
#include "rss_filter.hpp"

namespace libtorrent
{

	namespace io = detail;

utorrent_webui::utorrent_webui(session& s, save_settings_interface* sett
	, auto_load* al, torrent_history* hist
	, rss_filter_handler* rss_filter
	, auth_interface const* auth)
	: m_ses(s)
	, m_al(al)
	, m_auth(auth)
	, m_settings(sett)
	, m_rss_filter(rss_filter)
	, m_hist(hist)
	, m_listener(NULL)
{
	if (m_auth == NULL)
	{
		const static no_auth n;
		m_auth = &n;
	}

	m_start_time = time(NULL);
	m_version = 1;

	boost::uint64_t seed = total_microseconds(time_now_hires() - min_time());
	m_token = to_hex(hasher((char const*)&seed, sizeof(seed)).final().to_string());

	m_params_model.save_path = ".";
	m_webui_cookie = "{}";

	if (m_settings)
	{
		m_params_model.save_path = m_settings->get_str("save_path", ".");
		m_params_model.flags
			= (m_settings->get_int("start_paused", 0) ? add_torrent_params::flag_paused : add_torrent_params::flag_auto_managed)
			| add_torrent_params::flag_update_subscribe;
		m_webui_cookie = m_settings->get_str("ut_webui_cookie", "{}");
		int port = m_settings->get_int("listen_port", -1);
		if (port != -1)
		{
			error_code ec;
			m_ses.listen_on(std::make_pair(port, port+1), ec);
		}
	}

	if (m_al) m_al->set_params_model(m_params_model);
}

utorrent_webui::~utorrent_webui() {}

struct method_handler
{
	char const* action_name;
	void (utorrent_webui::*fun)(std::vector<char>&, char const* args, permissions_interface const* p);
};

static method_handler handlers[] =
{
	{ "start", &utorrent_webui::start },
	{ "forcestart", &utorrent_webui::force_start },
	{ "stop", &utorrent_webui::stop },
	{ "pause", &utorrent_webui::stop },
	{ "unpause", &utorrent_webui::start },

	{ "queueup", &utorrent_webui::queue_up },
	{ "queuedown", &utorrent_webui::queue_down },
	{ "queuetop", &utorrent_webui::queue_top },
	{ "queuebottom", &utorrent_webui::queue_bottom },

	{ "getfiles", &utorrent_webui::send_file_list },
	{ "getpeers", &utorrent_webui::send_peer_list },
	{ "getprops", &utorrent_webui::get_properties },
	{ "recheck", &utorrent_webui::recheck },
	{ "remove", &utorrent_webui::remove_torrent },
	{ "setprio", &utorrent_webui::set_file_priority },
	{ "getsettings", &utorrent_webui::get_settings },
	{ "setsetting", &utorrent_webui::set_settings },
	{ "add-url", &utorrent_webui::add_url },
//	{ "setprops", &utorrent_webui:: },
	{ "removedata", &utorrent_webui::remove_torrent_and_data },
	{ "list-dirs", &utorrent_webui::list_dirs },
	{ "rss-update", &utorrent_webui::rss_update },
	{ "rss-remove", &utorrent_webui::rss_remove },
	{ "filter-update", &utorrent_webui::rss_filter_update },
	{ "filter-remove", &utorrent_webui::rss_filter_remove },
	{ "removetorrent", &utorrent_webui::remove_torrent },
	{ "removedatatorrent", &utorrent_webui::remove_torrent_and_data },
	{ "getversion", &utorrent_webui::get_version },
//	{ "add-peer", &utorrent_webui:: },
};

void return_error(mg_connection* conn)
{
	mg_printf(conn, "HTTP/1.1 400 Invalid Request\r\n"
		"Content-Length: 0\r\n\r\n");
}

bool utorrent_webui::handle_http(mg_connection* conn, mg_request_info const* request_info)
{
	// redirect to /gui/
	if (strcmp(request_info->uri, "/gui") == 0
		|| (strcmp(request_info->uri, "/gui/") == 0
			&& request_info->query_string == NULL))
	{
		mg_printf(conn, "HTTP/1.1 301 Moved Permanently\r\n"
			"Content-Length: 0\r\n"
			"Location: /gui/index.html\r\n\r\n");
		return true;
	}

	// we only provide access to paths under /gui
	if (!string_begins_no_case("/gui/", request_info->uri)) return false;

	permissions_interface const* perms = parse_http_auth(conn, m_auth);
	if (!perms)
	{
		mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
			"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
			"Content-Length: 0\r\n\r\n");
		return true;
	}

	std::vector<char> response;

	// Auth token handling
	if (strcmp(request_info->uri, "/gui/token.html") == 0)
	{
		// note, the uTorrent webUI actually requires the xml attributes
		// to use single quotes here.
		appendf(response, "<html><div id='token' style='display:none;'>%s</div></html>"
			, m_token.c_str());
		TORRENT_ASSERT(response.back() != '\0');
		response.push_back('\0');

		mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			"Content-Length: %d\r\n"
			"Content-Type: text/html\r\n\r\n"
			"%s", int(response.size()-1), &response[0]);
		return true;
	}

	if (strcmp(request_info->uri, "/gui/") != 0) return false;

	if (request_info->query_string == NULL)
	{
		mg_printf(conn, "HTTP/1.1 400 Invalid Request (no query string)\r\n"
			"Connection: close\r\n\r\n");
		return true;
	}

	m_listener = (webui_base*)request_info->user_data;

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
			if (!perms->allow_add())
			{
				mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
					"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
					"Content-Length: 0\r\n\r\n");
				return true;
			}
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

				(this->*handlers[i].fun)(response, request_info->query_string, perms);
				break;
			}
		}
	}

	char buf[10];
	if (mg_get_var(request_info->query_string, strlen(request_info->query_string)
		, "list", buf, sizeof(buf)) > 0
		&& atoi(buf) > 0)
	{
		send_torrent_list(response, request_info->query_string, perms);
		send_rss_list(response, request_info->query_string, perms);
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
//	printf("%s\n", &response[0]);
	return true;
}

#define TORRENT_APPLY_FUN \
	std::vector<torrent_status> t = parse_torrents(args); \
	for (std::vector<torrent_status>::iterator i = t.begin() \
		, end(t.end()); i != end; ++i)

void utorrent_webui::start(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_start()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.clear_error();
		i->handle.set_upload_mode(false);
		i->handle.auto_managed(true);
		i->handle.resume();
	}
}

void utorrent_webui::stop(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_stop()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.auto_managed(false);
		i->handle.pause();
	}
}

void utorrent_webui::force_start(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_start()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.auto_managed(false);
		i->handle.resume();
	}
}

void utorrent_webui::recheck(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_recheck()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.force_recheck();
	}
}

void utorrent_webui::queue_up(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.queue_position_up();
	}
}

void utorrent_webui::queue_down(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.queue_position_down();
	}
}

void utorrent_webui::queue_top(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.queue_position_top();
	}
}

void utorrent_webui::queue_bottom(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_queue_change()) return;

	TORRENT_APPLY_FUN
	{
		i->handle.queue_position_bottom();
	}
}

void utorrent_webui::remove_torrent(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_remove()) return;

	TORRENT_APPLY_FUN
	{
		m_ses.remove_torrent(i->handle);
	}
}

void utorrent_webui::set_file_priority(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_set_file_prio()) return;

	char prio_str[10];
	if (mg_get_var(args, strlen(args)
		, "p", prio_str, sizeof(prio_str)) <= 0)
	{
		return;
	}
	int prio = atoi(prio_str);
	prio *= 2;

	std::vector<int> files;
	for (char const* f = strstr(args, "&f="); f; f = strstr(f, "&f="))
	{
		f += 3;
		char* end;
		int idx = strtol(f, &end, 10);
		if (*end == '&' || *end == '\0')
		{
			files.push_back(idx);
			f = end;
		}
	}

	TORRENT_APPLY_FUN
	{
		for (std::vector<int>::iterator j = files.begin(), end(files.end()); j != end; ++j)
			i->handle.file_priority(*j, prio);
	}
}

void utorrent_webui::remove_torrent_and_data(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_remove() || !p->allow_remove_data()) return;

	TORRENT_APPLY_FUN
	{
		m_ses.remove_torrent(i->handle, session::delete_files);
	}
}

void utorrent_webui::list_dirs(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	appendf(response, ", \"download-dirs\": [{\"path\":\"%s\",\"available\":%"PRId64"}]"
		, escape_json(m_params_model.save_path).c_str()
		, free_disk_space(m_params_model.save_path) / 1024 / 1024);
}

#undef TORRENT_APPLY_FUN

char const* settings_name(int s)
{
	return name_for_setting(s);
}

void utorrent_webui::get_settings(std::vector<char>& response, char const* args
	, permissions_interface const* p)
{
	appendf(response, ", \"settings\": [");

	libtorrent::aux::session_settings sett = m_ses.get_settings();

	// type: 0 = int, 1= bool, 2=string
	int first = 1;
	for (int i = 0; i < settings_pack::num_string_settings; ++i)
	{
		if (!p->allow_get_settings(settings_pack::string_type_base + i)) continue;

		int s = settings_pack::string_type_base + i;
		appendf(response, ",[\"%s\",2,\"%s\",{\"access\":\"%c\"}]\n" + first
			, settings_name(s), escape_json(sett.get_str(s)).c_str()
			, p->allow_set_settings(s) ? 'Y' : 'R');
		first = 0;
	}

	for (int i = 0; i < settings_pack::num_bool_settings; ++i)
	{
		int s = settings_pack::bool_type_base + i;
		if (!p->allow_get_settings(s)) continue;

		char const* sname;
		bool value;
		if (s == settings_pack::use_read_cache)
		{
			sname = "cache.read";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::enable_dht)
		{
			sname = "dht";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::enable_lsd)
		{
			sname = "lsd";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::enable_natpmp)
		{
			sname = "natpmp";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::enable_upnp)
		{
			sname = "upnp";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::auto_manage_prefer_seeds)
		{
			sname = "seeds_prioritized";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::use_write_cache)
		{
			sname = "cache.write";
			value = sett.get_bool(s);
		}
		else if (s == settings_pack::use_read_cache)
		{
			sname = "cache.read";
			value = sett.get_bool(s);
		}
		else
		{
			sname = settings_name(s);
			value = sett.get_bool(s);
		}
		appendf(response, ",[\"%s\",1,\"%s\",{\"access\":\"%c\"}]\n" + first
			, sname, value ? "true" : "false"
			, p->allow_set_settings(s) ? 'Y' : 'R');
		first = 0;
	}

	for (int i = 0; i < settings_pack::num_int_settings; ++i)
	{
		int s = settings_pack::int_type_base + i;
		if (!p->allow_get_settings(s)) continue;

		char const* sname;
		boost::int64_t value;
		if (s == settings_pack::cache_size)
		{
			sname = "cache.override_size";
			value = boost::int64_t(sett.get_int(s)) * 16 / 1024;
		}
		else if (s == settings_pack::upload_rate_limit)
		{
			sname = "max_ul_rate";
			value = boost::int64_t(sett.get_int(s)) / 1024;
		}
		else if (s == settings_pack::download_rate_limit)
		{
			sname = "max_dl_rate";
			value = boost::int64_t(sett.get_int(s)) / 1024;
		}
		else if (s == settings_pack::connections_limit)
		{
			sname = "conns_globally";
			value = sett.get_int(s);
		}
		else if (s == settings_pack::active_downloads)
		{
			sname = "max_active_downloads";
			value = sett.get_int(s);
		}
		else if (s == settings_pack::active_limit)
		{
			sname = "max_active_torrent";
			value = (std::min)(sett.get_int(s), sett.get_int(settings_pack::active_seeds));
		}
		else
		{
			sname = settings_name(s);
			value = sett.get_int(s);
		}

		appendf(response, ",[\"%s\",0,\"%"PRId64"\",{\"access\":\"%c\"}]\n" + first
			, sname, value, p->allow_set_settings(s) ? 'Y' : 'R');
		first = 0;
	}

	appendf(response, ",[\"torrents_start_stopped\",1,\"%s\",{\"access\":\"%c\"}]\n" + first
		, m_params_model.flags & add_torrent_params::flag_paused ? "true" : "false"
		, p->allow_stop() ? 'Y' : 'R');
	first = 0;

	if (m_al)
	{
		appendf(response,
			",[\"dir_autoload\",2,\"%s\",{\"access\":\"%c\"}]\n"
			",[\"dir_autoload_flag\",1,\"%s\",{\"access\":\"%c\"}]" + first
			, escape_json(m_al->auto_load_dir()).c_str()
			, p->allow_set_settings(-1) ? 'Y' : 'R'
			, m_al->scan_interval() ? "true" : "false"
			, p->allow_set_settings(-1) ? 'Y' : 'R');
		first = 0;
	}

	if (p->allow_get_settings(settings_pack::enable_outgoing_tcp)
		&& p->allow_get_settings(settings_pack::enable_outgoing_utp)
		&& p->allow_get_settings(settings_pack::enable_incoming_tcp)
		&& p->allow_get_settings(settings_pack::enable_incoming_utp))
	{
		appendf(response,
			",[\"bt.transp_disposition\",0,\"%d\",{\"access\":\"%c\"}]\n" + first
			, (sett.get_bool(settings_pack::enable_outgoing_tcp) ? 1 : 0)
				+ (sett.get_bool(settings_pack::enable_outgoing_utp) ? 2 : 0)
				+ (sett.get_bool(settings_pack::enable_incoming_tcp) ? 4 : 0)
				+ (sett.get_bool(settings_pack::enable_incoming_utp) ? 8 : 0)
				, (p->allow_set_settings(settings_pack::enable_outgoing_tcp)
					&& p->allow_set_settings(settings_pack::enable_outgoing_utp)
					&& p->allow_set_settings(settings_pack::enable_incoming_tcp)
					&& p->allow_set_settings(settings_pack::enable_incoming_utp))
				? 'Y' : 'R');
		first = 0;
	}

	if (p->allow_get_settings(-1))
	{
		appendf(response,
			",[\"dir_active_download\",2,\"%s\",{\"access\":\"%c\"}]\n"
			",[\"bind_port\",0,\"%d\",{\"access\":\"%c\"}]\n"
			+ first
			, escape_json(m_params_model.save_path).c_str()
			, p->allow_set_settings(-1) ? 'Y' : 'R'
			, m_ses.listen_port()
			, p->allow_set_settings(-1) ? 'Y' : 'R');
	}

	if (m_settings)
	{
		appendf(response,
			",[\"gui.default_del_action\",0,\"%d\",{\"access\":\"%c\"}]\n"
			, m_settings->get_int("default_del_action", 0)
			, p->allow_set_settings(-1) ? 'Y' : 'R');
	}

	appendf(response,
		",[\"webui.cookie\",2,\"%s\",{\"access\":\"Y\"}]\n"
		",[\"language\",0,\"0\",{\"access\":\"Y\"}]\n"
		",[\"webui.enable_listen\",1,\"true\",{\"access\":\"R\"}]\n"
		",[\"webui.enable_guest\",1,\"false\",{\"access\":\"R\"}]\n"
		",[\"webui.port\",0,\"%d\",{\"access\":\"R\"}]\n"
		",[\"cache.override\",1,\"true\",{\"access\":\"R\"}]\n"
		// the webUI uses the existence of this setting as an
		// indication of supporting the getpeers action, so we
		// need to define it in order to support peers
		",[\"webui.uconnect_enable\",1,\"false\",{\"access\":\"R\"}]\n"
		"]"
		 + first
		, escape_json(m_webui_cookie).c_str(), m_listener->listen_port());
	first = 0;
}
bool to_bool(std::string const& s)
{
	return s != "false" && s != "0";
}

void utorrent_webui::set_settings(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	settings_pack pack;

	std::set<std::string> duplicates;
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

		// ignore duplicate settings
		if (duplicates.count(key)) continue;
		duplicates.insert(key);

		s = v_end;

		if (ec) continue;

		if (key == "webui.cookie")
		{
			// TODO: store this in some session-specific store, so multiple
			// users don't clobber each other
			m_webui_cookie = value;
			if (m_settings) m_settings->set_str("ut_webui_cookie", value);
		}
		else if (key == "bind_port")
		{
			if (!p->allow_set_settings(-1)) continue;
			int port = atoi(value.c_str());
			error_code ec;
			m_ses.listen_on(std::make_pair(port, port+1), ec);
			if (m_settings) m_settings->set_int("listen_port", port);
		}
		else if (key == "bt.transp_disposition")
		{
			if (!p->allow_set_settings(settings_pack::enable_outgoing_tcp)
			|| !p->allow_set_settings(settings_pack::enable_outgoing_utp)
			|| !p->allow_set_settings(settings_pack::enable_incoming_tcp)
			|| !p->allow_set_settings(settings_pack::enable_incoming_utp))
				continue;

			int mask = atoi(value.c_str());
			pack.set_bool(settings_pack::enable_outgoing_tcp, mask & 1);
			pack.set_bool(settings_pack::enable_outgoing_utp, mask & 2);
			pack.set_bool(settings_pack::enable_incoming_tcp, mask & 4);
			pack.set_bool(settings_pack::enable_incoming_utp, mask & 8);
		}
		else if (key == "conns_globally")
		{
			if (!p->allow_set_settings(settings_pack::connections_limit)) continue;
			pack.set_int(settings_pack::connections_limit, atoi(value.c_str()));
		}
		else if (key == "max_active_downloads")
		{
			if (!p->allow_set_settings(settings_pack::active_downloads)) continue;
			pack.set_int(settings_pack::active_downloads, atoi(value.c_str()));
		}
		else if (key == "max_active_torrent")
		{
			if (!p->allow_set_settings(settings_pack::active_limit)) continue;
			if (!p->allow_set_settings(settings_pack::active_seeds)) continue;
			pack.set_int(settings_pack::active_limit, atoi(value.c_str()));
			pack.set_int(settings_pack::active_seeds, atoi(value.c_str()));
		}
		else if (key == "seeds_prioritized")
		{
			if (!p->allow_set_settings(settings_pack::auto_manage_prefer_seeds)) continue;
			pack.set_bool(settings_pack::auto_manage_prefer_seeds, to_bool(value));
		}
		else if (key == "torrents_start_stopped")
		{
			if (!p->allow_stop()) continue;
			bool b = to_bool(value);
			if (b)
			{
				m_params_model.flags = (m_params_model.flags
					& ~add_torrent_params::flag_auto_managed)
					| add_torrent_params::flag_paused;
			}
			else
			{
				m_params_model.flags = (m_params_model.flags
					| add_torrent_params::flag_auto_managed)
					& ~add_torrent_params::flag_paused;
			}
			if (m_al)
				m_al->set_params_model(m_params_model);
			m_settings->set_int("start_paused", b);
		}
		else if (key == "dir_autoload" && m_al)
		{
			if (!p->allow_set_settings(-1)) continue;
			m_al->set_auto_load_dir(value);
		}
		else if (key == "dir_autoload_flag" && m_al)
		{
			if (!p->allow_set_settings(-1)) continue;
			m_al->set_scan_interval(to_bool(value) ? 0 : 20);
		}
		else if (key == "dir_active_download")
		{
			if (!p->allow_set_settings(-1)) continue;
			m_params_model.save_path = value;
			if (m_al)
				m_al->set_params_model(m_params_model);
			if (m_settings) m_settings->set_str("save_path", value);
		}
		else if (key == "cache.override_size")
		{
			if (!p->allow_set_settings(settings_pack::cache_size)) continue;

			int size = atoi(value.c_str()) * 1024 / 16;
			pack.set_int(settings_pack::cache_size, size);
		}
		else if (key == "cache.write")
		{
			if (!p->allow_set_settings(settings_pack::use_write_cache)) continue;
			pack.set_bool(settings_pack::use_write_cache, to_bool(value));
		}
		else if (key == "cache.read")
		{
			if (!p->allow_set_settings(settings_pack::use_read_cache)) continue;
			pack.set_bool(settings_pack::use_read_cache, to_bool(value));
		}
		else if (key == "max_ul_rate")
		{
			if (!p->allow_set_settings(settings_pack::upload_rate_limit)) continue;
			pack.set_int(settings_pack::upload_rate_limit, atoi(value.c_str()) * 1024);
		}
		else if (key == "max_dl_rate")
		{
			if (!p->allow_set_settings(settings_pack::download_rate_limit)) continue;
			pack.set_int(settings_pack::download_rate_limit, atoi(value.c_str()) * 1024);
		}
		else if (key == "dht")
		{
			if (!p->allow_set_settings(settings_pack::enable_dht)) continue;
			pack.set_bool(settings_pack::enable_dht, to_bool(value));
		}
		else if (key == "natpmp")
		{
			if (!p->allow_set_settings(settings_pack::enable_natpmp)) continue;
			pack.set_bool(settings_pack::enable_natpmp, to_bool(value));
		}
		else if (key == "upnp")
		{
			if (!p->allow_set_settings(settings_pack::enable_upnp)) continue;
			pack.set_bool(settings_pack::enable_upnp, to_bool(value));
		}
		else if (key == "lsd")
		{
			if (!p->allow_set_settings(settings_pack::enable_lsd)) continue;
			pack.set_bool(settings_pack::enable_lsd, to_bool(value));
		}
		else if (key == "gui.default_del_action" && m_settings)
		{
			m_settings->set_int("default_del_action", atoi(value.c_str()));
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
					if (!p->allow_set_settings(field)) continue;
					pack.set_str(field, value.c_str());
					break;
				case settings_pack::int_type_base:
					if (!p->allow_set_settings(field)) continue;
					pack.set_int(field, atoi(value.c_str()));
					break;
				case settings_pack::bool_type_base:
					if (!p->allow_set_settings(field)) continue;
					pack.set_bool(field, to_bool(value));
					break;
			}
		}
	}
	m_ses.apply_settings(pack);

	error_code ec;
	if (m_settings) m_settings->save(ec);
}

void utorrent_webui::send_file_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	std::vector<torrent_status> t = parse_torrents(args);
	appendf(response, ",\"files\":[");
	int first = 1;
	std::vector<boost::int64_t> progress;
	std::vector<int> file_prio;
	for (std::vector<torrent_status>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		i->handle.file_progress(progress);
		file_prio = i->handle.file_priorities();
		boost::shared_ptr<const torrent_info> ti = i->torrent_file.lock();
		if (!ti || !ti->is_valid()) continue;
		file_storage const& files = ti->files();

		appendf(response, ",\"%s\",["+first, to_hex(ti->info_hash().to_string()).c_str());
		int first_file = 1;
		for (int i = 0; i < files.num_files(); ++i)
		{
			int first_piece = files.file_offset(i) / files.piece_length();
			int last_piece = (files.file_offset(i) + files.file_size(i)) / files.piece_length();
			// don't round 1 down to 0. 0 is special (do-not-download)
			if (file_prio[i] == 1) file_prio[i] = 2;
			appendf(response, ",[\"%s\", %"PRId64", %"PRId64", %d" + first_file
				, escape_json(files.file_name(i)).c_str()
				, files.file_size(i)
				, progress[i]
				, file_prio[i] / 2 // uTorrent's web UI uses 4 priority levels, libtorrent uses 8
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

void utorrent_webui::add_url(std::vector<char>&, char const* args, permissions_interface const* p)
{
	if (!p->allow_add()) return;

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

	add_torrent_params atp = m_params_model;
	atp.url = url;

	m_ses.async_add_torrent(atp);
}

void utorrent_webui::get_properties(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	std::vector<torrent_status> t = parse_torrents(args);
	appendf(response, ",\"props\":[");
	int first = 1;
	for (std::vector<torrent_status>::iterator i = t.begin()
		, end(t.end()); i != end; ++i)
	{
		torrent_status const& st = *i;
		boost::shared_ptr<const torrent_info> ti = st.torrent_file.lock();
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
			, trackers_as_string(i->handle).c_str()
			, st.handle.download_limit()
			, st.handle.upload_limit()
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
		ret += (pi.flags & peer_info::choked) ? 'u' : 'U';
	}
	else if (!(pi.flags & peer_info::choked))
	{
		// ERROR: we're unchoking someone that isn't interested
		ret += '?';
	}

	if (pi.flags & peer_info::interesting)
	{
		ret += (pi.flags & peer_info::remote_choked) ? 'd' : 'D';
	}
	else if (!(pi.flags & peer_info::remote_choked))
	{
		// ERROR: we're being unchoked even though we're not interested
		ret += 'K';
	}

	if (pi.flags & peer_info::optimistic_unchoke)
		ret += 'O';

	if (pi.flags & peer_info::snubbed)
		ret += 'S';

	// separate flags from sources with a space
	ret += ' ';

	if (!(pi.flags & peer_info::local_connection))
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

	if (pi.flags & peer_info::utp_socket)
		ret += 'P';
	return ret;
}

void utorrent_webui::send_peer_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	std::vector<torrent_status> torrents = parse_torrents(args);
	appendf(response, ",\"peers\":[");
	int first = 1;
	for (std::vector<torrent_status>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{
		boost::shared_ptr<const torrent_info> ti = i->torrent_file.lock();
		if (!ti || !ti->is_valid()) continue;

		appendf(response, ",\"%s\",[" + first
			, to_hex(i->info_hash.to_string()).c_str());

		int first_peer = 1;
		std::vector<peer_info> peers;
		i->handle.get_peer_info(peers);
		for (std::vector<peer_info>::iterator p = peers.begin()
			, pend(peers.end()); p != pend; ++p)
		{
			appendf(response, ",[\"%c%c\",\"%s\",\"%s\",%d,%d,\"%s\",\"%s\",%d,%d,%d,%d,%d"
				",%d,%"PRId64",%"PRId64",%d,%d,%d,%d,%d,%d,%d]" + first_peer
				, isprint(p->country[0]) ? p->country[0] : ' '
				, isprint(p->country[1]) ? p->country[1] : ' '
				, print_endpoint(p->ip).c_str()
				, ""
				, (p->flags & peer_info::utp_socket) != 0
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

void utorrent_webui::get_version(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	appendf(response, ",\"version\":{\"engine_version\": \"%s\""
		",\"major_version\": %d"
		",\"minor_version\": %d"
		",\"peer_id\": \"%s\""
		",\"user_agent\": \"%s\""
		",\"product_code\": \"server\""
		"}"
		, LIBTORRENT_REVISION, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR
		, to_hex(m_ses.id().to_string()).c_str(), m_ses.get_settings().get_str(settings_pack::user_agent).c_str());
}

void utorrent_webui::rss_update(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	char buf[20];
	int ret = mg_get_var(args, strlen(args), "feed-id", buf, sizeof(buf));
	int feed_id = atoi(buf);

	ret = mg_get_var(args, strlen(args), "subscribe", buf, sizeof(buf));
	int subscribe = atoi(buf);

	ret = mg_get_var(args, strlen(args), "update", buf, sizeof(buf));
	int update = atoi(buf);
	
	char url[2048];
	ret = mg_get_var(args, strlen(args), "url", url, sizeof(url));

	if (feed_id == -1)
	{
		feed_settings f;
		f.url = url;
		f.auto_download = subscribe;
		f.add_args = m_params_model;
		m_ses.add_feed(f);
	}
	else
	{
		// TOOD: iterate over all feeds to find the one with feed_id
		// TOOD: if update is set, just refresh the feed
	}
}

int get_feed_id(feed_status const& st)
{
	sha1_hash h = hasher(st.url.c_str(), st.url.length()).final();
	char const* ptr = (char const*)&h[0];
	return io::read_uint32(ptr) & 0x7fffffff;
}

void utorrent_webui::rss_remove(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	char buf[20];
	int ret = mg_get_var(args, strlen(args), "feed-id", buf, sizeof(buf));
	if (ret < 0) return;
	int feed_id = atoi(buf);

	std::vector<feed_handle> f;
	m_ses.get_feeds(f);

	for (std::vector<feed_handle>::iterator i = f.begin()
		, end(f.end()); i != end; ++i)
	{
		feed_status st = i->get_feed_status();
		if (get_feed_id(st) != feed_id) continue;
		m_ses.remove_feed(*i);
		return;
	}
}

void utorrent_webui::rss_filter_update(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	char buf[512];

	int ret = mg_get_var(args, strlen(args), "filter-id", buf, sizeof(buf));
	if (ret < 0) return;
	int filter_id = atoi(buf);

	rss_rule r;
	if (filter_id == -1)
	{
		r.params = m_params_model;
	}
	else
	{
		r = m_rss_filter->get_rule(filter_id);
	}
		
	ret = mg_get_var(args, strlen(args), "filter", buf, sizeof(buf));
	if (ret > 0) r.search = buf;
	ret = mg_get_var(args, strlen(args), "not-filter", buf, sizeof(buf));
	if (ret > 0) r.search_not = buf;
	ret = mg_get_var(args, strlen(args), "origname", buf, sizeof(buf));
	if (ret > 0) r.exact_match = atoi(buf);
	ret = mg_get_var(args, strlen(args), "add-stopped", buf, sizeof(buf));
	if (ret > 0)
	{
		if (atoi(buf))
		{
			r.params.flags = (r.params.flags
				& ~add_torrent_params::flag_auto_managed)
				| add_torrent_params::flag_paused;
		}
		else
		{
			r.params.flags = (r.params.flags
				| add_torrent_params::flag_auto_managed)
				& ~add_torrent_params::flag_paused;
		}
	}
	ret = mg_get_var(args, strlen(args), "smart-ep-filter", buf, sizeof(buf));
	if (ret > 0) r.episode_filter = atoi(buf);
	ret = mg_get_var(args, strlen(args), "save-in", buf, sizeof(buf));
	if (ret > 0) r.params.save_path = buf;
	ret = mg_get_var(args, strlen(args), "name", buf, sizeof(buf));
	if (ret > 0) r.name = buf;

	if (filter_id == -1)
	{
		if (r.name.empty()) r.name = "New Filter";
		int filter_id = m_rss_filter->add_rule(r);
		appendf(response, ",\"filter_ident\": %d", filter_id);
	}
	else
	{
		m_rss_filter->edit_rule(r);
	}
}

void utorrent_webui::rss_filter_remove(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	char buf[512];

	int ret = mg_get_var(args, strlen(args), "filter-id", buf, sizeof(buf));
	if (ret < 0) return;
	int filter_id = atoi(buf);
	m_rss_filter->remove_rule(filter_id);
	if (m_hist)
	{
		int cid = m_hist->frame();
		m_removed_rss_filters.push_back(std::make_pair(cid, filter_id));
	}
	while (m_removed_rss_filters.size() > 40)
		m_removed_rss_filters.pop_front();
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
	if (!st.paused && (st.state == torrent_status::downloading
		|| st.state == torrent_status::downloading_metadata
		|| st.state == torrent_status::seeding
		|| st.state == torrent_status::finished))
		ret |= STARTED;

	if (!st.paused && (st.state == torrent_status::queued_for_checking || st.state == torrent_status::checking_files))
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
	if (st.upload_mode) return "Upload Mode";

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

void utorrent_webui::send_torrent_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

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
		boost::shared_ptr<const torrent_info> ti = i->torrent_file.lock();
		appendf(response, ",[\"%s\",%d,\"%s\",%"PRId64",%d,%"PRId64",%"PRId64",%f,%d,%d,%d,\"%s\",%d,%d,%d,%d,%d,%d,%"PRId64"" + first
			, to_hex(i->info_hash.to_string()).c_str()
			, utorrent_status(*i)
			, escape_json(i->name).c_str()
			, ti ? ti->total_size() : 0
			, i->progress_ppm / 1000
			, i->all_time_download
			, i->all_time_upload
			, i->all_time_download == 0 ? 0 : float(i->all_time_upload) * 1000.f / i->all_time_download
			, i->upload_payload_rate
			, i->download_payload_rate
			, i->download_payload_rate == 0 ? 0 : (i->total_wanted - i->total_wanted_done) / i->download_payload_rate
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
			, escape_json(utorrent_message(*i)).c_str()
			, to_hex(i->info_hash.to_string()).c_str()
			, i->added_time
			, i->completed_time
			, "" // app
			, escape_json(i->save_path).c_str()
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

void utorrent_webui::send_rss_list(std::vector<char>& response, char const* args, permissions_interface const* p)
{
	if (!p->allow_list()) return;

	int cid = 0;
	char buf[50];
	// first, find the action
	int ret = mg_get_var(args, strlen(args)
		, "cid", buf, sizeof(buf));
	if (ret > 0) cid = atoi(buf);

	appendf(response, cid > 0 ? ",\"rssfeedp\":[" : ",\"rssfeeds\":[");

	std::vector<feed_handle> feeds;
	m_ses.get_feeds(feeds);

	int first = 1;
	for (std::vector<feed_handle>::iterator i = feeds.begin()
		, end(feeds.end()); i != end; ++i)
	{
		feed_status st = i->get_feed_status();
		int id = get_feed_id(st);
/*
	from documentation:

		IDENT (integer),
		ENABLED (boolean),
		USE FEED TITLE (boolean),
		USER SELECTED (boolean),
		PROGRAMMED (boolean),
		DOWNLOAD STATE (integer),
		URL (string),
		NEXT UPDATE (integer in unix time)

		[
			NAME (string),
			NAME FULL (string),
			URL (string),
			QUALITY (integer),
			CODEC (integer),
			TIMESTAMP (integer),
			SEASON (integer),
			EPISODE (integer),
			EPISODE TO (integer),
			FEED ID (integer),
			REPACK (boolean),
			IN HISTORY (boolean)
		]
*/
		appendf(response, ",[%u,true,true,false,true,%u,\"%s\",%"PRId64",[" + first
			, id, 0, st.url.c_str(), 0);

		int first2 = 1;
		for (std::vector<feed_item>::iterator k = st.items.begin()
			, end(st.items.end()); k != end; ++k)
		{
			item_properties p;
			parse_name(k->title, p);
			appendf(response, ",[\"%s\",\"%s\",\"%s\",%u,0,%"PRId64",%u,%u,0,0,%u,false,false]" + first2
				, k->title.c_str(), k->title.c_str(), k->url.c_str(), 0, 0, p.season, p.episode, id);
			first2 = 0;
		}

		response.push_back(']');
		response.push_back(']');
		first = 0;
	}

	// TODO: support removing feeds
	appendf(response, "], \"rssfeedm\": []");

/*
	from documentation:

	[ IDENT (integer),
	FLAGS (integer),
	NAME (string),
	FILTER (string as regexp),
	NOT FILTER (string as regexp),
	DIRECTORY (string),
	FEED (integer as feed ID),
	QUALITY (integer in bytes),
	LABEL (string),
	POSTPONE MODE (integer),
	LAST MATCH (integer),
	SMART EP FILTER (integer),
	REPACK EP FILTER (integer),
	EPISODE FILTER STR (string),
	EPISODE FILTER (boolean),
	RESOLVING CANDIDATE (boolean) ],
*/
	appendf(response, cid > 0 ? ",\"rssfilterp\":[" : ",\"rssfilters\":[");

	if (m_rss_filter)
	{
		std::vector<rss_rule> rules = m_rss_filter->get_rules();
		first = 1;
		for (std::vector<rss_rule>::iterator i = rules.begin()
			, end(rules.end()); i != end; ++i)
		{
			appendf(response, ",[%d, %d, \"%s\", \"%s\", \"%s\", \"%s\", -1, 0, \"\", 0, 0, 0, 0, \"\", false, false]" + first
				, i->id // id
				, (i->episode_filter ? 8 : 0)
					| (i->exact_match ? 2 : 0)
					| ((i->params.flags & add_torrent_params::flag_paused) ? 16 : 0)
				, i->name.c_str() // name
				, i->search.c_str() // filter string
				, i->search_not.c_str() // not-filter string
				, i->params.save_path.c_str()); // directory
			first = 0;
		}
	}
	appendf(response, "],\"rssfilterm\":[");
	if (cid > 0)
	{
		first =1;
		for (std::deque<std::pair<int, int> >::iterator i = m_removed_rss_filters.begin()
			, end(m_removed_rss_filters.end()); i != end; ++i)
		{
			if (i->first <= cid) continue;
			appendf(response, ",%d" + first, i->second);
			first = 0;
		}
	}
	appendf(response, "]");
}

std::vector<torrent_status> utorrent_webui::parse_torrents(char const* args) const
{
	std::vector<torrent_status> ret;

	for (char const* hash = strstr(args, "&hash="); hash; hash = strstr(hash, "&hash="))
	{
		hash += 6;
		char const* end = strchr(hash, '&');
		if (end != NULL && end - hash != 40) continue;
		if (end == NULL && strlen(hash) != 40) continue;
		sha1_hash h;
		bool ok = from_hex(hash, 40, (char*)&h[0]);
		if (!ok) continue;
		torrent_status ts = m_hist->get_torrent_status(h);
		if (!ts.handle.is_valid()) continue;
		ret.push_back(ts);
	}
	return ret;
}

}

