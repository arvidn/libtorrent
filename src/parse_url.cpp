/*

Copyright (c) 2008-2009, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>

#include "libtorrent/parse_url.hpp"
#include "libtorrent/string_util.hpp"

namespace libtorrent {

	// returns protocol, auth, hostname, port, path
	std::tuple<std::string, std::string, std::string, int, std::string>
		parse_url_components(string_view url, error_code& ec)
	{
		std::string hostname; // hostname only
		std::string auth; // user:pass
		std::string protocol; // http or https for instance
		int port = -1;

		string_view::iterator at;
		string_view::iterator colon;
		string_view::iterator port_pos;

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

}
