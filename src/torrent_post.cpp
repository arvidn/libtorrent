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

using namespace libtorrent;

bool parse_torrent_post(mg_connection* conn, add_torrent_params& params, error_code& ec)
{
	char const* cl = mg_get_header(conn, "content-length");
	if (cl == NULL) return false;

	std::vector<char> post_body;

	int content_length = atoi(cl);
	if (content_length > 0 && content_length < 10 * 1024 * 1024)
	{
		post_body.resize(content_length + 1);
		// minus one here since we shouldn't read the null terminator
		mg_read(conn, &post_body[0], content_length);
		post_body[content_length] = 0;
		// null terminate
	}

	// expect a multipart message here
	char const* content_type = mg_get_header(conn, "content-type");
	if (strstr(content_type, "multipart/form-data") == NULL) return false;

	char const* boundary = strstr(content_type, "boundary=");
	if (boundary == NULL) return false;

	boundary += 9;

	char const* body_end = &post_body[0] + content_length;

	char const* part_start = strnstr(&post_body[0], boundary, content_length);
	if (part_start == NULL) return false;

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
		if (disposition != "application/octet-stream"
			&& disposition != "application/x-bittorrent") continue;

		char const* torrent_start = part.get_body().begin;
		error_code ec;
		params.ti = boost::intrusive_ptr<torrent_info>(new torrent_info(torrent_start, part_end - torrent_start, ec));
		if (ec) return false;
		return true;
	}

	return false;
}

