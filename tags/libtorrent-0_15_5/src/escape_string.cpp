/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <string>
#include <cctype>
#include <algorithm>
#include <limits>
#include <cstring>

#include <boost/optional.hpp>
#include <boost/array.hpp>
#include <boost/tuple/tuple.hpp>

#include "libtorrent/assert.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/parse_url.hpp"

#ifdef TORRENT_WINDOWS
#if TORRENT_USE_WPATH
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#endif

#include "libtorrent/utf8.hpp"

#if TORRENT_USE_LOCALE_FILENAMES
#include <iconv.h>
#include <locale.h>
#endif 

namespace libtorrent
{

	// lexical_cast's result depends on the locale. We need
	// a well defined result
	boost::array<char, 3 + std::numeric_limits<size_type>::digits10> to_string(size_type n)
	{
		boost::array<char, 3 + std::numeric_limits<size_type>::digits10> ret;
		char *p = &ret.back();;
		*p = '\0';
		unsigned_size_type un = n;
		if (n < 0)  un = -un;
		do {
			*--p = '0' + un % 10;
			un /= 10;
		} while (un);
		if (n < 0) *--p = '-';
		std::memmove(&ret.front(), p, sizeof(ret.elems));
		return ret;
	}

	bool is_digit(char c)
	{
		return c >= '0' && c <= '9';
	}

	bool is_print(char c)
	{
		return c >= 32 && c < 127;
	}

	bool is_space(char c)
	{
		const static char* ws = " \t\n\r\f\v";
		return bool(std::strchr(ws, c));
	}

	char to_lower(char c)
	{
		return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
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

	std::string unescape_string(std::string const& s, error_code& ec)
	{
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
		{
			if(*i == '+')
			{
				ret += ' ';
			}
			else if (*i != '%')
			{
				ret += *i;
			}
			else
			{
				++i;
				if (i == s.end())
				{
					ec = errors::invalid_escaped_string;
					return ret;
				}

				int high;
				if(*i >= '0' && *i <= '9') high = *i - '0';
				else if(*i >= 'A' && *i <= 'F') high = *i + 10 - 'A';
				else if(*i >= 'a' && *i <= 'f') high = *i + 10 - 'a';
				else
				{
					ec = errors::invalid_escaped_string;
					return ret;
				}

				++i;
				if (i == s.end())
				{
					ec = errors::invalid_escaped_string;
					return ret;
				}

				int low;
				if(*i >= '0' && *i <= '9') low = *i - '0';
				else if(*i >= 'A' && *i <= 'F') low = *i + 10 - 'A';
				else if(*i >= 'a' && *i <= 'f') low = *i + 10 - 'a';
				else
				{
					ec = errors::invalid_escaped_string;
					return ret;
				}

				ret += char(high * 16 + low);
			}
		}
		return ret;
	}

	// http://www.ietf.org/rfc/rfc2396.txt
	// section 2.3
	static const char unreserved_chars[] =
		// when determining if a url needs encoding
		// % should be ok
		"%+"
		// reserved
		";?:@=&,$/"
		// unreserved (special characters) ' excluded,
		// since some buggy trackers fail with those
		"-_!.~*()"
		// unreserved (alphanumerics)
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789";

	static const char hex_chars[] = "0123456789abcdef";

	// the offset is used to ignore the first characters in the unreserved_chars table.
	static std::string escape_string_impl(const char* str, int len, int offset)
	{
		TORRENT_ASSERT(str != 0);
		TORRENT_ASSERT(len >= 0);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(offset < sizeof(unreserved_chars)-1);

		std::string ret;
		for (int i = 0; i < len; ++i)
		{
			if (std::strchr(unreserved_chars+offset, *str) && *str != 0)
			{
				ret += *str;
			}
			else
			{
				ret += '%';
				ret += hex_chars[((unsigned char)*str) >> 4];
				ret += hex_chars[((unsigned char)*str) & 15];
			}
			++str;
		}
		return ret;
	}
	
	std::string escape_string(const char* str, int len)
	{
		return escape_string_impl(str, len, 11);
	}

	std::string escape_path(const char* str, int len)
	{
		return escape_string_impl(str, len, 10);
	}

	bool need_encoding(char const* str, int len)
	{
		for (int i = 0; i < len; ++i)
		{
			if (std::strchr(unreserved_chars, *str) == 0 || *str == 0)
				return true;
			++str;
		}
		return false;
	}
	
	std::string read_until(char const*& str, char delim, char const* end)
	{
		TORRENT_ASSERT(str <= end);

		std::string ret;
		while (str != end && *str != delim)
		{
			ret += *str;
			++str;
		}
		// skip the delimiter as well
		while (str != end && *str == delim) ++str;
		return ret;
	}

	std::string maybe_url_encode(std::string const& url)
	{
		std::string protocol, host, auth, path;
		int port;
		error_code ec;
		boost::tie(protocol, auth, host, port, path) = parse_url_components(url, ec);
		if (ec) return url;
		
		// first figure out if this url contains unencoded characters
		if (!need_encoding(path.c_str(), path.size()))
			return url;

		char msg[NAME_MAX*4];
		snprintf(msg, sizeof(msg), "%s://%s%s%s:%d%s", protocol.c_str(), auth.c_str()
			, auth.empty()?"":"@", host.c_str(), port
			, escape_path(path.c_str(), path.size()).c_str());
		return msg;
	}

	std::string base64encode(const std::string& s)
	{
		static const char base64_table[] =
		{
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
			'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
			'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
			'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
			'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
			'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
			'w', 'x', 'y', 'z', '0', '1', '2', '3',
			'4', '5', '6', '7', '8', '9', '+', '/'
		};

		unsigned char inbuf[3];
		unsigned char outbuf[4];
	
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end();)
		{
			// available input is 1,2 or 3 bytes
			// since we read 3 bytes at a time at most
			int available_input = (std::min)(3, int(s.end()-i));

			// clear input buffer
			std::fill(inbuf, inbuf+3, 0);

			// read a chunk of input into inbuf
			std::copy(i, i + available_input, inbuf);
			i += available_input;

			// encode inbuf to outbuf
			outbuf[0] = (inbuf[0] & 0xfc) >> 2;
			outbuf[1] = ((inbuf[0] & 0x03) << 4) | ((inbuf [1] & 0xf0) >> 4);
			outbuf[2] = ((inbuf[1] & 0x0f) << 2) | ((inbuf [2] & 0xc0) >> 6);
			outbuf[3] = inbuf[2] & 0x3f;

			// write output
			for (int j = 0; j < available_input+1; ++j)
			{
				ret += base64_table[outbuf[j]];
			}

			// write pad
			for (int j = 0; j < 3 - available_input; ++j)
			{
				ret += '=';
			}
		}
		return ret;
	}

	std::string base32encode(std::string const& s)
	{
		static const char base32_table[] =
		{
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
			'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
			'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
			'Y', 'Z', '2', '3', '4', '5', '6', '7'
		};

		int input_output_mapping[] = {0, 2, 4, 5, 7, 8};
		
		unsigned char inbuf[5];
		unsigned char outbuf[8];
	
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end();)
		{
			int available_input = (std::min)(5, int(s.end()-i));

			// clear input buffer
			std::fill(inbuf, inbuf+5, 0);

			// read a chunk of input into inbuf
			std::copy(i, i + available_input, inbuf);
			i += available_input;

			// encode inbuf to outbuf
			outbuf[0] = (inbuf[0] & 0xf8) >> 3;
			outbuf[1] = ((inbuf[0] & 0x07) << 2) | ((inbuf[1] & 0xc0) >> 6);
			outbuf[2] = ((inbuf[1] & 0x3e) >> 1);
			outbuf[3] = ((inbuf[1] & 0x01) << 4) | ((inbuf[2] & 0xf0) >> 4);
			outbuf[4] = ((inbuf[2] & 0x0f) << 1) | ((inbuf[3] & 0x80) >> 7);
			outbuf[5] = ((inbuf[3] & 0x7c) >> 2);
			outbuf[6] = ((inbuf[3] & 0x03) << 3) | ((inbuf[4] & 0xe0) >> 5);
			outbuf[7] = inbuf[4] & 0x1f;

			// write output
			int num_out = input_output_mapping[available_input];
			for (int j = 0; j < num_out; ++j)
			{
				ret += base32_table[outbuf[j]];
			}

			// write pad
			for (int j = 0; j < 8 - num_out; ++j)
			{
				ret += '=';
			}
		}
		return ret;
	}

	std::string base32decode(std::string const& s)
	{
		unsigned char inbuf[8];
		unsigned char outbuf[5];
	
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end();)
		{
			int available_input = (std::min)(8, int(s.end()-i));

			int pad_start = 0;
			if (available_input < 8) pad_start = available_input;

			// clear input buffer
			std::fill(inbuf, inbuf+8, 0);
			for (int j = 0; j < available_input; ++j)
			{
				char in = std::toupper(*i++);
				if (in >= 'A' && in <= 'Z')
					inbuf[j] = in - 'A';
				else if (in >= '2' && in <= '7')
					inbuf[j] = in - '2' + ('Z' - 'A') + 1;
				else if (in == '=')
				{
					inbuf[j] = 0;
					if (pad_start == 0) pad_start = j;
				}
				else if (in == '1')
					inbuf[j] = 'I' - 'A';
				else
					return std::string();
				TORRENT_ASSERT(inbuf[j] == (inbuf[j] & 0x1f));
			}

			// decode inbuf to outbuf
			outbuf[0] = inbuf[0] << 3;
			outbuf[0] |= inbuf[1] >> 2;
			outbuf[1] = (inbuf[1] & 0x3) << 6;
			outbuf[1] |= inbuf[2] << 1;
			outbuf[1] |= (inbuf[3] & 0x10) >> 4;
			outbuf[2] = (inbuf[3] & 0x0f) << 4;
			outbuf[2] |= (inbuf[4] & 0x1e) >> 1;
			outbuf[3] = (inbuf[4] & 0x01) << 7;
			outbuf[3] |= (inbuf[5] & 0x1f) << 2;
			outbuf[3] |= (inbuf[6] & 0x18) >> 3;
			outbuf[4] = (inbuf[6] & 0x07) << 5;
			outbuf[4] |= inbuf[7];

			int input_output_mapping[] = {5, 1, 1, 2, 2, 3, 4, 4, 5};
			int num_out = input_output_mapping[pad_start];

			// write output
			std::copy(outbuf, outbuf + num_out, std::back_inserter(ret));
		}
		return ret;
	}

	boost::optional<std::string> url_has_argument(
		std::string const& url, std::string argument, size_t* out_pos)
	{
		size_t i = url.find('?');
		if (i == std::string::npos) return boost::optional<std::string>();
		++i;

		argument += '=';

		if (url.compare(i, argument.size(), argument) == 0)
		{
			size_t pos = i + argument.size();
			if (out_pos) *out_pos = pos;
			return url.substr(pos, url.find('&', pos) - pos);
		}
		argument.insert(0, "&");
		i = url.find(argument, i);
		if (i == std::string::npos) return boost::optional<std::string>();
		size_t pos = i + argument.size();
		if (out_pos) *out_pos = pos;
		return url.substr(pos, url.find('&', pos) - pos);
	}

	TORRENT_EXPORT std::string to_hex(std::string const& s)
	{
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
		{
			ret += hex_chars[((unsigned char)*i) >> 4];
			ret += hex_chars[((unsigned char)*i) & 0xf];
		}
		return ret;
	}

	TORRENT_EXPORT void to_hex(char const *in, int len, char* out)
	{
		for (char const* end = in + len; in < end; ++in)
		{
			*out++ = hex_chars[((unsigned char)*in) >> 4];
			*out++ = hex_chars[((unsigned char)*in) & 0xf];
		}
		*out = '\0';
	}

	int hex_to_int(char in)
	{
		if (in >= '0' && in <= '9') return int(in) - '0';
		if (in >= 'A' && in <= 'F') return int(in) - 'A' + 10;
		if (in >= 'a' && in <= 'f') return int(in) - 'a' + 10;
		return -1;
	}

	TORRENT_EXPORT bool is_hex(char const *in, int len)
	{
		for (char const* end = in + len; in < end; ++in)
		{
			int t = hex_to_int(*in);
			if (t == -1) return false;
		}
		return true;
	}

	TORRENT_EXPORT bool from_hex(char const *in, int len, char* out)
	{
		for (char const* end = in + len; in < end; ++in, ++out)
		{
			int t = hex_to_int(*in);
			if (t == -1) return false;
			*out = t << 4;
			++in;
			t = hex_to_int(*in);
			if (t == -1) return false;
			*out |= t & 15;
		}
		return true;
	}

#if TORRENT_USE_WPATH
	std::wstring convert_to_wstring(std::string const& s)
	{
		std::wstring ret;
		int result = libtorrent::utf8_wchar(s, ret);
#ifndef BOOST_WINDOWS
		return ret;
#else
		if (result == 0) return ret;

		ret.clear();
		const char* end = &s[0] + s.size();
		for (const char* i = &s[0]; i < end;)
		{
			wchar_t c = '.';
			int result = std::mbtowc(&c, i, end - i);
			if (result > 0) i += result;
			else ++i;
			ret += c;
		}
		return ret;
#endif
	}
#endif

#ifdef TORRENT_WINDOWS
	std::string convert_to_native(std::string const& s)
	{
#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			std::wstring ws;
			libtorrent::utf8_wchar(s, ws);
			std::size_t size = wcstombs(0, ws.c_str(), 0);
			if (size == std::size_t(-1)) return s;
			std::string ret;
			ret.resize(size);
			size = wcstombs(&ret[0], ws.c_str(), size + 1);
			if (size == std::size_t(-1)) return s;
			ret.resize(size);
			return ret;
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch(std::exception)
		{
			return s;
		}
#endif
	}

#elif TORRENT_USE_LOCALE_FILENAMES
	std::string convert_to_native(std::string const& s)
	{
		// the empty string represents the local dependent encoding
		static iconv_t iconv_handle = iconv_open("", "UTF-8");
		if (iconv_handle == iconv_t(-1)) return s;
		std::string ret;
		size_t insize = s.size();
		size_t outsize = insize * 4;
		ret.resize(outsize);
		char const* in = s.c_str();
		char* out = &ret[0];
		size_t retval = iconv(iconv_handle, (char**)&in, &insize,
			&out, &outsize);
		if (retval == (size_t)-1) return s;
		// if this string has an invalid utf-8 sequence in it, don't touch it
		if (insize != 0) return s;
		// not sure why this would happen, but it seems to be possible
		if (outsize > s.size() * 4) return s;
		// outsize is the number of bytes unused of the out-buffer
		TORRENT_ASSERT(ret.size() >= outsize);
		ret.resize(ret.size() - outsize);
		return ret;
	}
#endif

}

