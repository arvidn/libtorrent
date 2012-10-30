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
}

utorrent_webui::~utorrent_webui() {}

struct method_handler
{
	char const* action_name;
	void (utorrent_webui::*fun)(std::vector<char>&, char const* args);
};

method_handler handlers[] =
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
//	{ "getsettings", &utorrent_webui:: },
//	{ "setsetting", &utorrent_webui:: },
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

	if (strncmp(request_info->uri, "/gui/", 5) != 0) return false;

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

	for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i)
	{
		if (strcmp(action, handlers[i].action_name)) continue;

		(this->*handlers[i].fun)(response, request_info->query_string);
		break;
	}

	// we need a null terminator
	response.push_back('\0');
	// subtract one from content-length
	// to not count null terminator
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
		"Content-Type: text/json\r\n"
		"Content-Length: %d\r\n\r\n", int(response.size()) - 1);
	mg_write(conn, &response[0], response.size());
//	printf("%s\n", &response[0]);
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
		ret.push_back(m_ses.find_torrent(h));
	}
	return ret;
}

}

