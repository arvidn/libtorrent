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

#include "torrent_post.hpp"
#include "transmission_webui.hpp"
#include "libtorrent/http_parser.hpp" // for http_parser

extern "C" {
#include "mongoose.h"
}

namespace libtorrent
{

torrent_post::torrent_post(session& s, std::string path)
	: m_ses(s)
	, m_path(path)
{
	m_params_model.save_path = ".";
}

bool torrent_post::handle_http(mg_connection* conn,
	mg_request_info const* request_info)
{
	std::string req_path = request_info->uri;
	if (request_info->query_string && m_path.find('?'))
	{
		req_path += '?';
		req_path += request_info->query_string;
	}
	if (req_path != m_path) return false;

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

	printf("REQUEST: %s?%s\n", request_info->uri
		, request_info->query_string ? request_info->query_string : "");

	// expect a multipart message here
	char const* content_type = mg_get_header(conn, "content-type");
	if (strstr(content_type, "multipart/form-data") == NULL)
	{
		mg_printf(conn, "HTTP/1.1 400 Invalid argument\r\n\r\n");
		return true;
	}

	char const* boundary = strstr(content_type, "boundary=");
	if (boundary == NULL)
	{
		mg_printf(conn, "HTTP/1.1 400 Missing boundary in header\r\n\r\n");
		return true;
	}
	boundary += 9;

	char const* body_end = &post_body[0] + post_body.size();

	char const* part_start = strnstr(&post_body[0], boundary, post_body.size());
	if (part_start == NULL)
	{
		mg_printf(conn, "HTTP/1.1 400 Missing boundary\r\n\r\n");
		return true;
	}
	part_start += strlen(boundary);
	char const* part_end = NULL;

	// loop through all parts
	for(; part_start < body_end; part_start = (std::min)(body_end, part_end + strlen(boundary)))
	{
		part_end = strnstr(part_start, boundary, body_end - part_start);
		if (part_end == NULL) part_end = body_end;

		http_parser part;
		bool error = false;
		part.incoming(buffer::const_interval(part_start, part_end), error);
/*
		std::multimap<std::string, std::string> const& part_headers = part.headers();
		for (std::multimap<std::string, std::string>::const_iterator i = part_headers.begin()
			, end(part_headers.end()); i != end; ++i)
		{
			printf("  %s: %s\n", i->first.c_str(), i->second.c_str());
		}
*/
		std::string const& disposition = part.header("content-type");
		if (disposition != "application/octet-stream") continue;

		add_torrent_params params = m_params_model;
		char const* torrent_start = part.get_body().begin;
		error_code ec;
		params.ti = boost::intrusive_ptr<torrent_info>(new torrent_info(torrent_start, part_end - torrent_start, ec));

		if (ec)
		{
			mg_printf(conn, "HTTP/1.1 400 %s\r\n\r\n", ec.message().c_str());
			continue;
		}

		std::string query_string = "?";
		query_string += request_info->query_string;

		std::string paused = url_has_argument(query_string, "paused");
		if (paused == "true")
		{
			params.flags |= add_torrent_params::flag_paused;
			params.flags &= ~add_torrent_params::flag_auto_managed;
		}

		torrent_handle h = m_ses.add_torrent(params);
		if (ec)
		{
			mg_printf(conn, "HTTP/1.1 400 %s\r\n\r\n", ec.message().c_str());
			return true;
		}
	}

	mg_printf(conn, "HTTP/1.1 200 OK\r\n\r\n");
	return true;
}

}

