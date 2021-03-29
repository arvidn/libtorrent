/*

Copyright (c) 2012, 2014-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2017, Pavel Pimenov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STRING_UTIL_HPP_INCLUDED
#define TORRENT_STRING_UTIL_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/error_code.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <limits>
#include <array> // for std::array

namespace lt::aux {

	TORRENT_EXTRA_EXPORT bool is_alpha(char c);

	TORRENT_EXTRA_EXPORT
		std::array<char, 4 + std::numeric_limits<std::int64_t>::digits10>
		to_string(std::int64_t n);

	// internal
	inline bool is_digit(char c)
	{ return c >= '0' && c <= '9'; }
	inline void ensure_trailing_slash(std::string& url)
	{
		if (url.empty() || url[url.size() - 1] != '/')
			url += '/';
	}

	// internal
	TORRENT_EXTRA_EXPORT string_view strip_string(string_view in);

	TORRENT_EXTRA_EXPORT bool is_print(char c);
	TORRENT_EXTRA_EXPORT bool is_space(char c);
	TORRENT_EXTRA_EXPORT char to_lower(char c);

	TORRENT_EXTRA_EXPORT bool string_begins_no_case(char const* s1, char const* s2);
	TORRENT_EXTRA_EXPORT bool string_equal_no_case(string_view s1, string_view s2);

	TORRENT_EXTRA_EXPORT void url_random(span<char> dest);

	TORRENT_EXTRA_EXPORT bool string_ends_with(string_view s1, string_view s2);

	// Returns offset at which src matches target.
	// If no sync found, return -1
	TORRENT_EXTRA_EXPORT int search(span<char const> src, span<char const> target);

	struct listen_interface_t
	{
		std::string device;
		int port;
		bool ssl;
		bool local;
		friend bool operator==(listen_interface_t const& lhs, listen_interface_t const& rhs)
		{
			return lhs.device == rhs.device
				&& lhs.port == rhs.port
				&& lhs.ssl == rhs.ssl
				&& lhs.local == rhs.local;
		}
	};

	// this parses the string that's used as the listen_interfaces setting.
	// it is a comma-separated list of IP or device names with ports. For
	// example: "eth0:6881,eth1:6881" or "127.0.0.1:6881"
	TORRENT_EXTRA_EXPORT std::vector<listen_interface_t> parse_listen_interfaces(
		std::string const& in, std::vector<std::string>& errors);

#if TORRENT_ABI_VERSION == 1 \
	|| !defined TORRENT_DISABLE_LOGGING
	TORRENT_EXTRA_EXPORT std::string print_listen_interfaces(
		std::vector<listen_interface_t> const& in);
#endif

	// this parses the string that's used as the listen_interfaces setting.
	// it is a comma-separated list of IP or device names with ports. For
	// example: "eth0:6881,eth1:6881" or "127.0.0.1:6881"
	TORRENT_EXTRA_EXPORT void parse_comma_separated_string_port(
		std::string const& in, std::vector<std::pair<std::string, int>>& out);

	// this parses the string that's used as the outgoing_interfaces setting.
	// it is a comma separated list of IPs and device names. For example:
	// "eth0, eth1, 127.0.0.1"
	TORRENT_EXTRA_EXPORT void parse_comma_separated_string(
		std::string const& in, std::vector<std::string>& out);

	// strdup is not part of the C standard. Some systems
	// don't have it and it won't be available when building
	// in strict ANSI mode
	TORRENT_EXTRA_EXPORT char* allocate_string_copy(string_view str);

	// searches for separator ('sep') in the string 'last'.
	// if found, returns the string_view representing the range from the start of
	// `last` up to (but not including) the separator. The second return value is
	// the remainder of the string, starting one character after the separator.
	// if no separator is found, the whole string is returned and the second
	// return value is an empty string_view.
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> split_string(string_view last, char sep);

	// same as split_string, but if one sub-string starts with a double quote
	// (") separators are ignored until the end double-quote. Unless if the
	// separator itself is a double quote.
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> split_string_quotes(
		string_view last, char sep);

	// removes whitespaces at the beginning of the string, in-place
	TORRENT_EXTRA_EXPORT void ltrim(std::string& s);

#if TORRENT_USE_I2P

	TORRENT_EXTRA_EXPORT bool is_i2p_url(std::string const& url);

#endif
}

#endif
