/*

Copyright (c) 2008-2016, Arvid Norberg
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

#include <algorithm>

#include "libtorrent/parse_url.hpp"
#include "libtorrent/string_util.hpp"

namespace libtorrent
{

	// returns protocol, auth, hostname, port, path
	std::tuple<std::string, std::string, std::string, int, std::string>
		parse_url_components(std::string url, error_code& ec)
	{
		std::string hostname; // hostname only
		std::string auth; // user:pass
		std::string protocol; // http or https for instance
		int port = -1;

		std::string::iterator at;
		std::string::iterator colon;
		std::string::iterator port_pos;

		// PARSE URL
		std::string::iterator start = url.begin();
		// remove white spaces in front of the url
		while (start != url.end() && is_space(*start))
			++start;
		std::string::iterator end
			= std::find(url.begin(), url.end(), ':');
		protocol.assign(start, end);

		if (end == url.end())
		{
			ec = errors::unsupported_url_protocol;
			goto exit;
		}
		++end;
		if (end == url.end() || *end != '/')
		{
			ec = errors::unsupported_url_protocol;
			goto exit;
		}
		++end;
		if (end == url.end() || *end != '/')
		{
			ec = errors::unsupported_url_protocol;
			goto exit;
		}
		++end;
		start = end;

		at = std::find(start, url.end(), '@');
		colon = std::find(start, url.end(), ':');
		end = std::find(start, url.end(), '/');

		if (at != url.end()
			&& colon != url.end()
			&& colon < at
			&& at < end)
		{
			auth.assign(start, at);
			start = at;
			++start;
		}

		// this is for IPv6 addresses
		if (start != url.end() && *start == '[')
		{
			port_pos = std::find(start, url.end(), ']');
			if (port_pos == url.end())
			{
				ec = errors::expected_close_bracket_in_address;
				goto exit;
			}
			// strip the brackets
			hostname.assign(start + 1, port_pos);
			port_pos = std::find(port_pos, url.end(), ':');
		}
		else
		{
			port_pos = std::find(start, url.end(), ':');
			if (port_pos < end) hostname.assign(start, port_pos);
			else hostname.assign(start, end);
		}

		if (port_pos < end)
		{
			++port_pos;
			for (std::string::iterator i = port_pos; i < end; ++i)
			{
				if (is_digit(*i)) continue;
				ec = errors::invalid_port;
				goto exit;
			}
			port = std::atoi(std::string(port_pos, end).c_str());
		}

		start = end;
exit:
		return std::make_tuple(std::move(protocol)
			, std::move(auth)
			, std::move(hostname)
			, port
			, std::string(start, url.end()));
	}

	// splits a url into the base url and the path
	std::tuple<std::string, std::string>
		split_url(std::string url, error_code& ec)
	{
		std::string base;
		std::string path;

		// PARSE URL
		std::string::iterator pos
			= std::find(url.begin(), url.end(), ':');

		if (pos == url.end() || url.end() - pos < 2
			|| *(pos + 1) != '/' || *(pos + 2) != '/')
		{
			ec = errors::unsupported_url_protocol;
			return std::make_tuple(url, path);
		}
		pos += 3; // skip "://"

		pos = std::find(pos, url.end(), '/');
		if (pos == url.end())
		{
			return std::make_tuple(std::move(url), std::move(path));
		}

		base.assign(url.begin(), pos);
		path.assign(pos, url.end());
		return std::make_tuple(std::move(base), std::move(path));
	}

}
