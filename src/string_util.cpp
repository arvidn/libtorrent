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

#include "libtorrent/config.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/parse_url.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/tuple/tuple.hpp>

#include <cstdlib> // for malloc
#include <cstring> // for memmov/strcpy/strlen

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{

	// We need well defined results that don't depend on locale
	boost::array<char, 4 + std::numeric_limits<boost::int64_t>::digits10>
		to_string(boost::int64_t n)
	{
		boost::array<char, 4 + std::numeric_limits<boost::int64_t>::digits10> ret;
		char *p = &ret.back();
		*p = '\0';
		boost::uint64_t un = n;
		// TODO: warning C4146: unary minus operator applied to unsigned type,
		// result still unsigned
		if (n < 0)  un = -un;
		do {
			*--p = '0' + un % 10;
			un /= 10;
		} while (un);
		if (n < 0) *--p = '-';
		std::memmove(&ret[0], p, &ret.back() - p + 1);
		return ret;
	}

	bool is_alpha(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	bool is_print(char c)
	{
		return c >= 32 && c < 127;
	}

	bool is_space(char c)
	{
		static const char* ws = " \t\n\r\f\v";
		return strchr(ws, c) != 0;
	}

	char to_lower(char c)
	{
		return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
	}

	int split_string(char const** tags, int buf_size, char* in)
	{
		int ret = 0;
		char* i = in;
		for (;*i; ++i)
		{
			if (!is_print(*i) || is_space(*i))
			{
				*i = 0;
				if (ret == buf_size) return ret;
				continue;
			}
			if (i == in || i[-1] == 0)
			{
				tags[ret++] = i;
			}
		}
		return ret;
	}

	bool string_begins_no_case(char const* s1, char const* s2)
	{
		while (*s1 != 0)
		{
			if (to_lower(*s1) != to_lower(*s2)) return false;
			++s1;
			++s2;
		}
		return true;
	}

	bool string_equal_no_case(char const* s1, char const* s2)
	{
		while (to_lower(*s1) == to_lower(*s2))
		{
			if (*s1 == 0) return true;
			++s1;
			++s2;
		}
		return false;
	}

	// generate a url-safe random string
	void url_random(char* begin, char* end)
	{
		// http-accepted characters:
		// excluding ', since some buggy trackers don't support that
		static char const printable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz-_.!~*()";

		// the random number
		while (begin != end)
			*begin++ = printable[random() % (sizeof(printable)-1)];
	}

	char* allocate_string_copy(char const* str)
	{
		if (str == 0) return 0;
		char* tmp = static_cast<char*>(std::malloc(std::strlen(str) + 1));
		if (tmp == 0) return 0;
		std::strcpy(tmp, str);
		return tmp;
	}

	// 8-byte align pointer
	void* align_pointer(void* p)
	{
		int offset = uintptr_t(p) & 0x7;
		// if we're already aligned, don't do anything
		if (offset == 0) return p;

		// offset is how far passed the last aligned address
		// we are. We need to go forward to the next aligned
		// one. Since aligned addresses are 8 bytes apart, add
		// 8 - offset.
		return static_cast<char*>(p) + (8 - offset);
	}

	// this parses the string that's used as the listen_interfaces setting.
	// it is a comma-separated list of IP or device names with ports. For
	// example: "eth0:6881,eth1:6881" or "127.0.0.1:6881"
	void parse_comma_separated_string_port(std::string const& in
		, std::vector<std::pair<std::string, int> >& out)
	{
		out.clear();

		std::string::size_type start = 0;
		std::string::size_type end = 0;

		while (start < in.size())
		{
			// skip leading spaces
			while (start < in.size()
				&& is_space(in[start]))
				++start;

			end = in.find_first_of(',', start);
			if (end == std::string::npos) end = in.size();

			std::string::size_type colon = in.find_last_of(':', end);

			if (colon != std::string::npos && colon > start)
			{
				int port = atoi(in.substr(colon + 1, end - colon - 1).c_str());

				// skip trailing spaces
				std::string::size_type soft_end = colon;
				while (soft_end > start
					&& is_space(in[soft_end-1]))
					--soft_end;

				// in case this is an IPv6 address, strip off the square brackets
				// to make it more easily parseable into an ip::address
				if (in[start] == '[') ++start;
				if (soft_end > start && in[soft_end-1] == ']') --soft_end;

				out.push_back(std::make_pair(in.substr(start, soft_end - start), port));
			}

			start = end + 1;
		}
	}

	void parse_comma_separated_string(std::string const& in, std::vector<std::string>& out)
	{
		out.clear();

		std::string::size_type start = 0;
		std::string::size_type end = 0;

		while (start < in.size())
		{
			// skip leading spaces
			while (start < in.size()
				&& is_space(in[start]))
				++start;

			end = in.find_first_of(',', start);
			if (end == std::string::npos) end = in.size();

			// skip trailing spaces
			std::string::size_type soft_end = end;
			while (soft_end > start
				&& is_space(in[soft_end-1]))
				--soft_end;

			out.push_back(in.substr(start, soft_end - start));
			start = end + 1;
		}
	}

	char* string_tokenize(char* last, char sep, char** next)
	{
		if (last == 0) return 0;
		if (last[0] == '"')
		{
			*next = strchr(last + 1, '"');
			// consume the actual separator as well.
			if (*next != NULL)
				*next = strchr(*next, sep);
		}
		else
		{
			*next = strchr(last, sep);
		}
		if (*next == 0) return last;
		**next = 0;
		++(*next);
		while (**next == sep && **next) ++(*next);
		return last;
	}

#if TORRENT_USE_I2P

	bool is_i2p_url(std::string const& url)
	{
		using boost::tuples::ignore;
		std::string hostname;
		error_code ec;
		boost::tie(ignore, ignore, hostname, ignore, ignore)
			= parse_url_components(url, ec);
		char const* top_domain = strrchr(hostname.c_str(), '.');
		return top_domain && strcmp(top_domain, ".i2p") == 0;
	}

#endif

	std::size_t string_hash_no_case::operator()(std::string const& s) const
	{
		size_t ret = 5381;
		for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
			ret = (ret * 33) ^ to_lower(*i);
		return ret;
	}

	bool string_eq_no_case::operator()(std::string const& lhs, std::string const& rhs) const
	{
		if (lhs.size() != rhs.size()) return false;

		std::string::const_iterator s1 = lhs.begin();
		std::string::const_iterator s2 = rhs.begin();

		while (s1 != lhs.end() && s2 != rhs.end())
		{
			if (to_lower(*s1) != to_lower(*s2)) return false;
			++s1;
			++s2;
		}
		return true;
	}

	bool string_less_no_case::operator()(std::string const& lhs, std::string const& rhs) const
	{
		std::string::const_iterator s1 = lhs.begin();
		std::string::const_iterator s2 = rhs.begin();

		while (s1 != lhs.end() && s2 != rhs.end())
		{
			char const c1 = to_lower(*s1);
			char const c2 = to_lower(*s2);
			if (c1 < c2) return true;
			if (c1 > c2) return false;
			++s1;
			++s2;
		}

		// this is the tie-breaker
		return lhs.size() < rhs.size();
	}

}

