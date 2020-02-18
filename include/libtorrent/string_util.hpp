/*

Copyright (c) 2012-2018, Arvid Norberg
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

namespace libtorrent {

	TORRENT_EXTRA_EXPORT bool is_alpha(char c);

	TORRENT_EXTRA_EXPORT
		std::array<char, 4+std::numeric_limits<std::int64_t>::digits10>
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

#if TORRENT_USE_I2P

	TORRENT_EXTRA_EXPORT bool is_i2p_url(std::string const& url);

#endif
}

#endif
