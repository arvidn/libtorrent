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
#include <boost/tuple/tuple.hpp>

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
#include "response_buffer.hpp" // for appendf

namespace libtorrent
{

utorrent_webui::utorrent_webui(session& s)
	: m_ses(s)
{
	m_params_model.save_path = ".";
	m_start_time = time(NULL);
	m_webui_cookie = "{}";
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

//	{ "getfiles", &utorrent_webui:: },
//	{ "getpeers", &utorrent_webui:: },
//	{ "getprops", &utorrent_webui:: },
	{ "recheck", &utorrent_webui::recheck },
	{ "remove", &utorrent_webui::remove_torrent },
//	{ "setprio", &utorrent_webui:: },
	{ "getsettings", &utorrent_webui::get_settings },
	{ "setsetting", &utorrent_webui::set_settings },
//	{ "add-file", &utorrent_webui:: },
//	{ "add-url", &utorrent_webui::add_url },
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
	mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n"
		"Content: close\r\n\r\n");
}

bool utorrent_webui::handle_http(mg_connection* conn, mg_request_info const* request_info)
{
	printf("REQUEST: %s?%s\n", request_info->uri
		, request_info->query_string ? request_info->query_string : "");

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

	if (strcmp(request_info->uri, "/gui/") != 0) return false;

	printf("REQUEST: %s?%s\n", request_info->uri
		, request_info->query_string ? request_info->query_string : "");

	char action[50];

	// first, find the action
	int ret = mg_get_var(request_info->query_string, strlen(request_info->query_string)
		, "action", action, sizeof(action));
	if (ret <= 0)
	{
		return_error(conn);
		return true;
	}

	std::vector<char> response;
	appendf(response, "{\"build\":%d", LIBTORRENT_VERSION_NUM);

	for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i)
	{
		if (strcmp(action, handlers[i].action_name)) continue;

		(this->*handlers[i].fun)(response, request_info->query_string);
		break;
	}

	char buf[10];
	if (mg_get_var(request_info->query_string, strlen(request_info->query_string)
		, "list", buf, sizeof(buf)) > 0)
	{
		print_torrent_list(response);
	}

	// we need a null terminator
	appendf(response, "}");
	response.push_back('\0');

	// subtract one from content-length
	// to not count null terminator
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
		"Content-Type: text/json\r\n"
		"Content-Length: %d\r\n\r\n", int(response.size()) - 1);
	mg_write(conn, &response[0], response.size());
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
			, settings_name(s), sett.get_str(s).c_str());
		first = 0;
	}

	for (int i = 0; i < settings_pack::num_bool_settings; ++i)
	{
		int s = settings_pack::bool_type_base + i;
		appendf(response, ",[\"%s\",1,\"%s\",{\"access\":\"Y\"}]\n" + first
			, settings_name(s), sett.get_bool(s) ? "true" : "false");
		first = 0;
	}

	for (int i = 0; i < settings_pack::num_int_settings; ++i)
	{
		int s = settings_pack::int_type_base + i;
		appendf(response, ",[\"%s\",0,\"%d\",{\"access\":\"Y\"}]\n" + first
			, settings_name(s), sett.get_int(s));
		first = 0;
	}

	appendf(response,
		",[\"bind_port\",0,\"%d\",{\"access\":\"Y\"}]\n"
		",[\"webui.cookie\",2,\"%s\",{\"access\":\"Y\"}]\n"
		",[\"language\",0,\"0\",{\"access\":\"Y\"}]\n"
		",[\"webui.enable_listen\",1,\"true\",{\"access\":\"Y\"}]\n"
		",[\"webui.enable_guest\",1,\"false\",{\"access\":\"Y\"}]\n"
		"]"
		 + first
		, m_ses.listen_port()
		, m_webui_cookie.c_str());
}

void utorrent_webui::set_settings(std::vector<char>& response, char const* args)
{
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

		if (key == "webui.cookie")
		{
			m_webui_cookie = value;
		}
		else if (key == "bind_port")
		{
			int port = atoi(value.c_str());
			error_code ec;
			m_ses.listen_on(std::make_pair(port, port+1), ec);
		}
		else
		{
			// ...
		}

		s = v_end;
	}
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

	if (st.state == torrent_status::queued_for_checking)
		return "Checking";

	if (st.state == torrent_status::checking_files)
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "Checking (%d.%2d%%)"
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

bool nop(torrent_status const&) { return true; }

void utorrent_webui::print_torrent_list(std::vector<char>& response)
{
	appendf(response, ", \"torrents\": [ \"torrentp\": [");

	std::vector<torrent_status> torrents;
	m_ses.get_torrent_status(&torrents, &nop);

	for (std::vector<torrent_status>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{

		int first = 1;
		boost::intrusive_ptr<torrent_info> ti = i->handle.torrent_file();
		appendf(response, ",[\"%s\",%d,\"%s\",%"PRId64",%d,%"PRId64",%"PRId64",%f,%d,%d,%d,\"\",%d,%d,%d,%d,%d,%d,%"PRId64",\"%s\",\"%s\",\"%s\",\"%s\",%"PRId64",%"PRId64",\"%s\",\"%s\",%d,\"%s\"]" + first
			, to_hex(i->handle.info_hash().to_string()).c_str()
			, utorrent_status(*i)
			, i->handle.name().c_str()
			, ti->total_size()
			, i->progress_ppm / 1000
			, i->total_payload_download
			, i->total_payload_upload
			, i->all_time_download == 0 ? 0 : float(i->all_time_upload) / i->all_time_download
			, i->upload_rate
			, i->download_rate
			, i->download_rate == 0 ? 0 : (i->total_wanted - i->total_wanted_done) / i->download_rate
			, i->num_peers - i->num_seeds
			, i->list_peers - i->list_seeds
			, i->num_seeds
			, i->list_seeds
			, int(i->distributed_full_copies << 16) + int(i->distributed_fraction * 65536 / 1000)
			, i->queue_position
			, i->total_wanted - i->total_wanted_done
			, "" // url this torrent came from
			, "" // feed URL this torrent belongs to
			, utorrent_message(*i).c_str()
			, "" // sid
			, i->added_time
			, i->completed_time
			, "" // app
			, i->handle.save_path().c_str()
			, 0
			, "");
		first = 0;
	}

	// TODO: support labels
	appendf(response, "], \"label\": [], \"torrentm\": [], \"torrentc\": \"0\"]");
}

std::vector<torrent_handle> utorrent_webui::parse_torrents(char const* args) const
{
	std::vector<torrent_handle> ret;

	for (char* hash = strstr(args, "&h="); hash; hash = strstr(hash, "&h="))
	{
		hash += 3;
		char const* end = strchr(hash, '&');
		if (end != NULL && end - hash != 40) continue;
		if (end == NULL && strlen(hash) != 40) continue;
		sha1_hash h;
		bool ok = from_hex(hash, 40, (char*)&h[0]);
		if (!ok) continue;
		ret.push_back(m_ses.find_torrent(h));
	}
	return ret;
}

}

