/*

Copyright (c) 2008-2009, 2013-2016, 2020-2021, Arvid Norberg
Copyright (c) 2016, 2018, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>

#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/string_util.hpp"
#include "libtorrent/string_view.hpp"

namespace libtorrent::aux {

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
		bool ipv6_literal = false;

		// PARSE URL
		auto start = url.begin();
		// remove white spaces in front of the url
		while (start != url.end() && aux::is_space(*start))
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
			ipv6_literal = true;
		}
		else
		{
			port_pos = std::find(start, url.end(), ':');
			if (port_pos < end) hostname.assign(start, port_pos);
			else hostname.assign(start, end);
		}

		if (hostname.empty())
		{
			ec = errors::invalid_hostname;
			goto exit;
		}
		// LDH from RFC 952/1123 for DNS hostnames; IPv6 literals additionally
		// allow ':', '%' (RFC 6874 zone separator '%25' parses as '%' + hex)
		// and '_' / '~' for zone-ID characters
		for (char const c : hostname)
		{
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '-' || c == '.')
				continue;
			if (ipv6_literal && (c == ':' || c == '%' || c == '_' || c == '~')) continue;
			ec = errors::invalid_hostname;
			goto exit;
		}

		if (port_pos < end)
		{
			++port_pos;
			for (auto i = port_pos; i < end; ++i)
			{
				if (aux::is_digit(*i)) continue;
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
			std::tie(arg, query_string) = aux::split_string(query_string, '&');

			auto const name = aux::split_string(arg, '=').first;
			for (auto const& tracker_arg : tracker_args)
			{
				if (aux::string_equal_no_case(name, tracker_arg))
					return true;
			}
		}
		return false;
	}

	bool same_origin(std::string const& a, std::string const& b)
	{
		// the origin is the (scheme, host, port) triple. Scheme and host are
		// compared case-insensitively. This is meant for http/https URLs: a
		// missing port is replaced by 443 for "https" and 80 otherwise.
		// userinfo, path and query are not part of the origin.
		auto const origin = [](std::string const& u) {
			error_code ec;
			std::string protocol, host, ignore;
			int port;
			std::tie(protocol, ignore, host, port, ignore) = parse_url_components(u, ec);
			if (ec) host.clear();
			if (port == -1) port = string_equal_no_case(protocol, "https") ? 443 : 80;
			return std::make_tuple(std::move(protocol), std::move(host), port);
		};
		auto const oa = origin(a);
		auto const ob = origin(b);
		return !std::get<1>(oa).empty() // parsed successfully (host non-empty)
			&& string_equal_no_case(std::get<0>(oa), std::get<0>(ob))
			&& string_equal_no_case(std::get<1>(oa), std::get<1>(ob))
			&& std::get<2>(oa) == std::get<2>(ob);
	}

	bool is_valid_tracker_url(string_view const url)
	{
		if (url.empty())
		{
			return false;
		}

		// reject control characters and space. RFC 3986 requires both to be
		// percent-encoded; control chars also enable header/CRLF injection
		for (char const c : url)
		{
			auto const uc = static_cast<unsigned char>(c);
			if (uc <= 0x20 || uc == 0x7f) return false;
		}

		if (!string_begins_no_case("http://", url) && !string_begins_no_case("https://", url)
#if TORRENT_USE_RTC
			&& !string_begins_no_case("wss://", url) && !string_begins_no_case("ws://", url)
#endif
			&& !string_begins_no_case("udp://", url))
		{
			return false;
		}

		error_code ec;
		parse_url_components(url, ec);
		return !ec;
	}

}
