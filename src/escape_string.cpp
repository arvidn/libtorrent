/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include <string>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <cstring>

#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/windows.hpp"
#endif

#if TORRENT_USE_ICONV
#include <iconv.h>
#include <locale.h>
#endif

#include "libtorrent/assert.hpp"
#include "libtorrent/parse_url.hpp"

#include "libtorrent/utf8.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/string_util.hpp" // for to_string
#include "libtorrent/aux_/array.hpp"

namespace libtorrent {

	// defined in hex.cpp
	namespace aux {

		extern const char hex_chars[];
	}

	std::string unescape_string(string_view s, error_code& ec)
	{
		std::string ret;
		for (auto i = s.begin(); i != s.end(); ++i)
		{
			if (*i == '+')
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
				if (*i >= '0' && *i <= '9') high = *i - '0';
				else if (*i >= 'A' && *i <= 'F') high = *i + 10 - 'A';
				else if (*i >= 'a' && *i <= 'f') high = *i + 10 - 'a';
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
	static char const unreserved_chars[] =
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

	namespace {

	// the offset is used to ignore the first characters in the unreserved_chars table.
	std::string escape_string_impl(const char* str, int const len, int const offset)
	{
		TORRENT_ASSERT(str != nullptr);
		TORRENT_ASSERT(len >= 0);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(offset < int(sizeof(unreserved_chars)) - 1);

		std::string ret;
		for (int i = 0; i < len; ++i)
		{
			if (std::strchr(unreserved_chars + offset, *str) && *str != 0)
			{
				ret += *str;
			}
			else
			{
				ret += '%';
				ret += aux::hex_chars[std::uint8_t(*str) >> 4];
				ret += aux::hex_chars[std::uint8_t(*str) & 15];
			}
			++str;
		}
		return ret;
	}

	} // anonymous namespace

	std::string escape_string(string_view str)
	{
		return escape_string_impl(str.data(), int(str.size()), 11);
	}

	std::string escape_path(string_view str)
	{
		return escape_string_impl(str.data(), int(str.size()), 10);
	}

	bool need_encoding(char const* str, int const len)
	{
		for (int i = 0; i < len; ++i)
		{
			if (std::strchr(unreserved_chars, *str) == nullptr || *str == 0)
				return true;
			++str;
		}
		return false;
	}

	void convert_path_to_posix(std::string& path)
	{
		std::replace(path.begin(), path.end(), '\\', '/');
	}

#ifdef TORRENT_WINDOWS
	void convert_path_to_windows(std::string& path)
	{
		std::replace(path.begin(), path.end(), '/', '\\');
	}
#endif

	// TODO: 2 this should probably be moved into string_util.cpp
	std::string read_until(char const*& str, char const delim, char const* end)
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
		std::tie(protocol, auth, host, port, path) = parse_url_components(url, ec);
		if (ec) return url;

		// first figure out if this url contains unencoded characters
		if (!need_encoding(path.c_str(), int(path.size())))
			return url;

		std::string msg;
		std::string escaped_path { escape_path(path) };
		// reserve enough space so further append will
		// only copy values to existing location
		msg.reserve(protocol.size() + 3 + // protocol part
			auth.size() + 1 + // auth part
			host.size() + // host part
			1 + 5 + // port part
			escaped_path.size());
		msg.append(protocol);
		msg.append("://");
		if (!auth.empty())
		{
			msg.append(auth);
			msg.append("@");
		}
		msg.append(host);
		if (port != -1)
		{
			msg.append(":");
			msg.append(to_string(port).data());
		}
		msg.append(escaped_path);

		return msg;
	}

#if TORRENT_ABI_VERSION == 1
	std::string resolve_file_url(std::string const& url)
	{
		TORRENT_ASSERT(url.substr(0, 7) == "file://");
		// first, strip the file:// part.
		// On windows, we have
		// to strip the first / as well
		std::size_t num_to_strip = 7;
#ifdef TORRENT_WINDOWS
		if (url[7] == '/' || url[7] == '\\') ++num_to_strip;
#endif
		std::string ret = url.substr(num_to_strip);

		// we also need to URL-decode it
		error_code ec;
		std::string unescaped = unescape_string(ret, ec);
		if (ec) unescaped = ret;

		// on windows, we need to convert forward slashes
		// to backslashes
#ifdef TORRENT_WINDOWS
		convert_path_to_windows(unescaped);
#endif

		return unescaped;
	}
#endif

	std::string base64encode(const std::string& s)
	{
		static char const base64_table[] =
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

		aux::array<std::uint8_t, 3> inbuf;
		aux::array<std::uint8_t, 4> outbuf;

		std::string ret;
		for (auto i = s.cbegin(); i != s.cend();)
		{
			// available input is 1,2 or 3 bytes
			// since we read 3 bytes at a time at most
			int available_input = std::min(int(inbuf.size()), int(s.end() - i));

			// clear input buffer
			inbuf.fill(0);

			// read a chunk of input into inbuf
			std::copy(i, i + available_input, inbuf.begin());
			i += available_input;

			// encode inbuf to outbuf
			outbuf[0] = (inbuf[0] & 0xfc) >> 2;
			outbuf[1] = (((inbuf[0] & 0x03) << 4) | ((inbuf [1] & 0xf0) >> 4)) & 0xff;
			outbuf[2] = (((inbuf[1] & 0x0f) << 2) | ((inbuf [2] & 0xc0) >> 6)) & 0xff;
			outbuf[3] = inbuf[2] & 0x3f;

			// write output
			for (int j = 0; j < available_input + 1; ++j)
			{
				ret += base64_table[outbuf[j]];
			}

			// write pad
			for (int j = 0; j < int(inbuf.size()) - available_input; ++j)
			{
				ret += '=';
			}
		}
		return ret;
	}

#if TORRENT_USE_I2P
	std::string base32encode(string_view s, encode_string_flags_t const flags)
	{
		static char const base32_table_canonical[] =
		{
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
			'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
			'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
			'Y', 'Z', '2', '3', '4', '5', '6', '7'
		};
		static char const base32_table_lowercase[] =
		{
			'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
			'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
			'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
			'y', 'z', '2', '3', '4', '5', '6', '7'
		};
		char const *base32_table = (flags & string::lowercase) ? base32_table_lowercase : base32_table_canonical;

		static aux::array<int, 6> const input_output_mapping{{{0, 2, 4, 5, 7, 8}}};

		aux::array<std::uint8_t, 5> inbuf;
		aux::array<std::uint8_t, 8> outbuf;

		std::string ret;
		for (auto i = s.begin(); i != s.end();)
		{
			int available_input = std::min(int(inbuf.size()), int(s.end() - i));

			// clear input buffer
			inbuf.fill(0);

			// read a chunk of input into inbuf
			std::copy(i, i + available_input, inbuf.begin());
			i += available_input;

			// encode inbuf to outbuf
			outbuf[0] = (inbuf[0] & 0xf8) >> 3;
			outbuf[1] = (((inbuf[0] & 0x07) << 2) | ((inbuf[1] & 0xc0) >> 6)) & 0xff;
			outbuf[2] = ((inbuf[1] & 0x3e) >> 1);
			outbuf[3] = (((inbuf[1] & 0x01) << 4) | ((inbuf[2] & 0xf0) >> 4)) & 0xff;
			outbuf[4] = (((inbuf[2] & 0x0f) << 1) | ((inbuf[3] & 0x80) >> 7)) & 0xff;
			outbuf[5] = ((inbuf[3] & 0x7c) >> 2);
			outbuf[6] = (((inbuf[3] & 0x03) << 3) | ((inbuf[4] & 0xe0) >> 5)) & 0xff;
			outbuf[7] = inbuf[4] & 0x1f;

			// write output
			int const num_out = input_output_mapping[available_input];
			for (int j = 0; j < num_out; ++j)
			{
				ret += base32_table[outbuf[j]];
			}

			if (!(flags & string::no_padding))
			{
				// write pad
				for (int j = 0; j < int(outbuf.size()) - num_out; ++j)
				{
					ret += '=';
				}
			}
		}
		return ret;
	}
#endif // TORRENT_USE_I2P

	std::string base32decode(string_view s)
	{
		aux::array<std::uint8_t, 8> inbuf;
		aux::array<std::uint8_t, 5> outbuf;

		std::string ret;
		for (auto i = s.begin(); i != s.end();)
		{
			int available_input = std::min(int(inbuf.size()), int(s.end() - i));

			int pad_start = 0;
			if (available_input < 8) pad_start = available_input;

			// clear input buffer
			inbuf.fill(0);
			for (int j = 0; j < available_input; ++j)
			{
				char const in = char(std::toupper(*i++));
				if (in >= 'A' && in <= 'Z')
					inbuf[j] = (in - 'A') & 0xff;
				else if (in >= '2' && in <= '7')
					inbuf[j] = (in - '2' + ('Z' - 'A') + 1) & 0xff;
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
			outbuf[0] = (inbuf[0] << 3) & 0xff;
			outbuf[0] |= inbuf[1] >> 2;
			outbuf[1] = ((inbuf[1] & 0x3) << 6) & 0xff;
			outbuf[1] |= inbuf[2] << 1;
			outbuf[1] |= (inbuf[3] & 0x10) >> 4;
			outbuf[2] = ((inbuf[3] & 0x0f) << 4) & 0xff;
			outbuf[2] |= (inbuf[4] & 0x1e) >> 1;
			outbuf[3] = ((inbuf[4] & 0x01) << 7) & 0xff;
			outbuf[3] |= (inbuf[5] & 0x1f) << 2;
			outbuf[3] |= (inbuf[6] & 0x18) >> 3;
			outbuf[4] = ((inbuf[6] & 0x07) << 5) & 0xff;
			outbuf[4] |= inbuf[7];

			static int const input_output_mapping[] = {5, 1, 1, 2, 2, 3, 4, 4, 5};
			int num_out = input_output_mapping[pad_start];

			// write output
			std::copy(outbuf.begin(), outbuf.begin() + num_out, std::back_inserter(ret));
		}
		return ret;
	}

	string_view trim(string_view str)
	{
		auto const first = str.find_first_not_of(" \t\n\r");
		auto const last = str.find_last_not_of(" \t\n\r");
		return str.substr(first == string_view::npos ? str.size() : first, last - first + 1);
	}

	string_view::size_type find(string_view haystack, string_view needle, string_view::size_type pos)
	{
		auto const p = haystack.substr(pos).find(needle);
		if (p == string_view::npos) return p;
		return pos + p;
	}

#if defined TORRENT_WINDOWS
	std::wstring convert_to_wstring(std::string const& s)
	{
		error_code ec;
		std::wstring ret = libtorrent::utf8_wchar(s, ec);
		if (!ec) return ret;

		ret.clear();
		const char* end = &s[0] + s.size();
		for (const char* i = &s[0]; i < end;)
		{
			wchar_t c = '.';
			int const result = std::mbtowc(&c, i, end - i);
			if (result > 0) i += result;
			else ++i;
			ret += c;
		}
		return ret;
	}

	std::string convert_from_wstring(std::wstring const& s)
	{
		error_code ec;
		std::string ret = libtorrent::wchar_utf8(s, ec);
		if (!ec) return ret;

		ret.clear();
		const wchar_t* end = &s[0] + s.size();
		for (const wchar_t* i = &s[0]; i < end;)
		{
			char c[10];
			TORRENT_ASSERT(sizeof(c) >= std::size_t(MB_CUR_MAX));
			int const result = std::wctomb(c, *i);
			if (result > 0)
			{
				i += result;
				ret.append(c, result);
			}
			else
			{
				++i;
				ret += ".";
			}
		}
		return ret;
	}
#endif

#if TORRENT_USE_ICONV
namespace {

	// this is a helper function to deduce the type of the second argument to
	// the iconv() function.

	template <typename Input>
	size_t call_iconv(size_t (&fun)(iconv_t, Input**, size_t*, char**, size_t*)
		, iconv_t cd, char const** in, size_t* insize, char** out, size_t* outsize)
	{
		return fun(cd, const_cast<Input**>(in), insize, out, outsize);
	}

	std::string iconv_convert_impl(std::string const& s, iconv_t h)
	{
		std::string ret;
		size_t insize = s.size();
		size_t outsize = insize * 4;
		ret.resize(outsize);
		char const* in = s.c_str();
		char* out = &ret[0];
		// posix has a weird iconv() signature. implementations
		// differ on the type of the second parameter. We use a helper template
		// to deduce what we need to cast to.
		std::size_t const retval = call_iconv(::iconv, h, &in, &insize, &out, &outsize);
		if (retval == size_t(-1)) return s;
		// if this string has an invalid utf-8 sequence in it, don't touch it
		if (insize != 0) return s;
		// not sure why this would happen, but it seems to be possible
		if (outsize > s.size() * 4) return s;
		// outsize is the number of bytes unused of the out-buffer
		TORRENT_ASSERT(ret.size() >= outsize);
		ret.resize(ret.size() - outsize);
		return ret;
	}
} // anonymous namespace

	std::string convert_to_native(std::string const& s)
	{
		static std::mutex iconv_mutex;
		// only one thread can use this handle at a time
		std::lock_guard<std::mutex> l(iconv_mutex);

		// the empty string represents the local dependent encoding
		static iconv_t iconv_handle = ::iconv_open("", "UTF-8");
		if (iconv_handle == iconv_t(-1)) return s;
		return iconv_convert_impl(s, iconv_handle);
	}

	std::string convert_from_native(std::string const& s)
	{
		static std::mutex iconv_mutex;
		// only one thread can use this handle at a time
		std::lock_guard<std::mutex> l(iconv_mutex);

		// the empty string represents the local dependent encoding
		static iconv_t iconv_handle = ::iconv_open("UTF-8", "");
		if (iconv_handle == iconv_t(-1)) return s;
		return iconv_convert_impl(s, iconv_handle);
	}

#elif defined TORRENT_WINDOWS

	std::string convert_to_native(std::string const& s)
	{
		std::wstring ws = libtorrent::utf8_wchar(s);
		std::string ret;
		ret.resize(ws.size() * 4 + 1);
		std::size_t size = WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, &ret[0], int(ret.size()), nullptr, nullptr);
		if (size == std::size_t(-1)) return s;
		if (size != 0 && ret[size - 1] == '\0') --size;
		ret.resize(size);
		return ret;
	}

	std::string convert_from_native(std::string const& s)
	{
		std::wstring ws;
		ws.resize(s.size() + 1);
		std::size_t size = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &ws[0], int(ws.size()));
		if (size == std::size_t(-1)) return s;
		if (size != 0 && ws[size - 1] == '\0') --size;
		ws.resize(size);
		return libtorrent::wchar_utf8(ws);
	}

#elif TORRENT_USE_LOCALE

	std::string convert_to_native(std::string const& s)
	{
		std::wstring ws = libtorrent::utf8_wchar(s);
		std::size_t size = wcstombs(0, ws.c_str(), 0);
		if (size == std::size_t(-1)) return s;
		std::string ret;
		ret.resize(size);
		size = wcstombs(&ret[0], ws.c_str(), size + 1);
		if (size == std::size_t(-1)) return s;
		ret.resize(size);
		return ret;
	}

	std::string convert_from_native(std::string const& s)
	{
		std::wstring ws;
		ws.resize(s.size());
		std::size_t size = mbstowcs(&ws[0], s.c_str(), s.size());
		if (size == std::size_t(-1)) return s;
		return libtorrent::wchar_utf8(ws);
	}

#endif

}
