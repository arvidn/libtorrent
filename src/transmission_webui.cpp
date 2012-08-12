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

#include "transmission_webui.hpp"
#include <string.h> // for strcmp() 
#include <stdio.h>
#include <vector>

extern "C" {
#include "mongoose.h"
#include "jsmn.h"
}

namespace libtorrent
{

jsmntok_t* next_token(jsmntok_t* i)
{
	++i;
	int n = i->size;
	++i;
	if (n == 0) return i;
	for (int k = 0; k < n; ++n)
		i = next_token(i);
	return i;
}

jsmntok_t* find_key(jsmntok_t* tokens, char* buf, char const* key, int type)
{
	if (tokens[0].type != JSMN_OBJECT) return NULL;
	jsmntok_t* end = &tokens[1] + tokens[0].size;
	for (jsmntok_t* i = &tokens[1]; i < end; i = next_token(i))
	{
		if (i->type != JSMN_STRING) continue;
		buf[i->end] = 0;
		if (strcmp(key, buf + i->start)) continue;
		if (i[1].type != type) continue;
		return i + 1;
	}
	return NULL;
}

void return_error(mg_connection* conn, char const* msg)
{
	// TODO: this should probably be more thought through
	mg_printf(conn, "{ \"error\": \"%s\"}", msg);
}

struct method_handler
{
	char const* method_name;
	void (transmission_webui::*fun)(mg_connection*, jsmntok_t* args, char* buffer);
};

method_handler handlers[] =
{
	{"torrent-add", &transmission_webui::add_torrent },
	{"torrent-get", &transmission_webui::get_torrent },
	{"torrent-set", &transmission_webui::set_torrent },
	{"torrent-start", &transmission_webui::start_torrent },
	{"torrent-start-now", &transmission_webui::start_torrent_now },
	{"torrent-stop", &transmission_webui::stop_torrent },
	{"torrent-verify", &transmission_webui::verify_torrent },
	{"torrent-reannounce", &transmission_webui::reannounce_torrent },
};

void transmission_webui::handle_json_rpc(mg_connection* conn, jsmntok_t* tokens, char* buffer)
{
	// we expect a "method" in the top level
	jsmntok_t* method = find_key(tokens, buffer, "method", JSMN_STRING);
	if (method == NULL)
	{
		return_error(conn, "missing method in request");
		return;
	}

	buffer[method->end] = 0;
	char const* m = &buffer[method->start];
	for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i)
	{
		if (strcmp(m, handlers[i].method_name)) continue;

		jsmntok_t* args = find_key(tokens, buffer, "arguments", JSMN_OBJECT);

		if (args) buffer[args->end] = 0;
		printf("%s: %s\n", m, args ? buffer + args->start : "{}");

		(this->*handlers[i].fun)(conn, args, buffer);
		break;
	}
}

void transmission_webui::add_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::get_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::set_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::start_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::start_torrent_now(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::stop_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::verify_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}
void transmission_webui::reannounce_torrent(mg_connection*, jsmntok_t* args, char* buffer)
{
}

transmission_webui::transmission_webui(session& s)
	: webui_base(s)
{}

transmission_webui::~transmission_webui() {}

bool transmission_webui::handle_http(mg_connection* conn, mg_request_info const* request_info)
{
	char const* cl = mg_get_header(conn, "content-length");
	std::vector<char> post_body;
	if (cl != NULL)
	{
		int content_length = atoi(cl);
		if (content_length > 0 && content_length < 10 * 1024 * 1024)
		{
			post_body.resize(content_length + 1);
			mg_read(conn, &post_body[0], post_body.size());
			post_body[content_length] = 0;
			// null terminate
		}
	}

	if (!strcmp(request_info->uri, "/transmission/rpc"))
	{
		if (post_body.empty())
		{
			return_error(conn, "request with no POST body");
			return true;
		}
		jsmntok_t tokens[256];
		jsmn_parser p;
		jsmn_init(&p);

		int r = jsmn_parse(&p, &post_body[0], tokens, sizeof(tokens)/sizeof(tokens[0]));
		if (r == JSMN_ERROR_INVAL)
		{
			return_error(conn, "request not JSON");
			return true;
		}
		else if (r == JSMN_ERROR_NOMEM)
		{
			return_error(conn, "request too big");
			return true;
		}
		else if (r == JSMN_ERROR_PART)
		{
			return_error(conn, "request truncated");
			return true;
		}
		else if (r != JSMN_SUCCESS)
		{
			return_error(conn, "invalid request");
			return true;
		}

		handle_json_rpc(conn, tokens, &post_body[0]);
		return true;
	}

	// TODO: handle other urls here

	return false;
}


}

