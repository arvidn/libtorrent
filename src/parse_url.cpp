/*

Copyright (c) 2008-2009, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
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
#include "libtorrent/string_view.hpp"

namespace libtorrent {

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
		auto start = url.begin();
		// remove white spaces in front of the url
		while (start != url.end() && is_space(*start))
			++start;
		auto end = std::find(url.begin(), url.end(), ':');
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
		end = std::min({
			std::find(start, url.end(), '/')
			, std::find(start, url.end(), '?')
			, std::find(start, url.end(), '#')
			});

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
			for (auto i = port_pos; i < end; ++i)
			{
				if (is_digit(*i)) continue;
				ec = errors::invalid_port;
				goto exit;
			}
			port = std::atoi(std::string(port_pos, end).c_str());
		}

		start = end;
exit:
		std::string path_component(start, url.end());
		if (path_component.empty()
			|| path_component.front() == '?'
			|| path_component.front() == '#')
		{
			path_component.insert(path_component.begin(), '/');
		}

		return std::make_tuple(std::move(protocol)
			, std::move(auth)
			, std::move(hostname)
			, port
			, path_component);
	}

	// splits a url into the base url and the path
	std::tuple<std::string, std::string>
		split_url(std::string url, error_code& ec)
	{
		std::string base;
		std::string path;

		// PARSE URL
		auto pos = std::find(url.begin(), url.end(), ':');

		if (pos == url.end() || url.end() - pos < 3
			|| *(pos + 1) != '/' || *(pos + 2) != '/')
		{
			ec = errors::unsupported_url_protocol;
			return std::make_tuple(std::move(url), std::move(path));
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

	TORRENT_EXTRA_EXPORT bool is_idna(string_view hostname)
	{
		for (;;)
		{
			auto dot = hostname.find('.');
			string_view const label = (dot == string_view::npos) ? hostname : hostname.substr(0, dot);
			if (label.size() >= 4
				&& (label[0] == 'x' || label[0] == 'X')
				&& (label[1] == 'n' || label[1] == 'N')
				&& label.substr(2, 2) == "--"_sv)
				return true;
			if (dot == string_view::npos) return false;
			hostname = hostname.substr(dot + 1);
		}
	}

	bool has_tracker_query_string(string_view query_string)
	{
		static string_view const tracker_args[] = {
			"info_hash"_sv, "event"_sv, "port"_sv, "left"_sv, "key"_sv,
			"uploaded"_sv, "downloaded"_sv, "corrupt"_sv, "peer_id"_sv
		};
		while (!query_string.empty())
		{
			string_view arg;
			std::tie(arg, query_string) = split_string(query_string, '&');

			auto const name = split_string(arg, '=').first;
			for (auto const& tracker_arg : tracker_args)
			{
				if (string_equal_no_case(name, tracker_arg))
					return true;
			}
		}
		return false;
	}

}
