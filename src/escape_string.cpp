/*

Copyright (c) 2004, 2006-2007, 2009-2011, 2013, 2015-2020, Arvid Norberg
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2016-2017, Andrei Kurushin
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
#include <vector>

#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/windows.hpp"
#else
#include <clocale>
#endif

#include "libtorrent/assert.hpp"
#include "libtorrent/parse_url.hpp"

#include "libtorrent/utf8.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/string_util.hpp" // for to_string
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/byteswap.hpp"

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

namespace {
	std::uint32_t map_base64_char(char const c)
	{
		if (c >= 'A' && c <= 'Z')
			return std::uint32_t(c - 'A');
		if (c >= 'a' && c <= 'z')
			return std::uint32_t(26 + c - 'a');
		if (c >= '0' && c <= '9')
			return std::uint32_t(52 + c - '0');
		if (c == '-') return 62;
		if (c == '~') return 63;
		throw system_error(error_code(lt::errors::invalid_escaped_string));
	}
}

	// this decodes the i2p alphabet
	std::vector<char> base64decode_i2p(string_view s)
	{
		std::uint32_t output = 0;

		std::vector<char> ret;
		int bit_offset = 18;
		for (auto const c : s)
		{
			if (c == '=') break;
			output |= map_base64_char(c) << bit_offset;
			if (bit_offset == 0)
			{
				output = aux::host_to_network(output);
				aux::array<char, 4> tmp;
				std::memcpy(tmp.data(), &output, 4);
				ret.push_back(tmp[1]);
				ret.push_back(tmp[2]);
				ret.push_back(tmp[3]);
				output = 0;
				bit_offset = 18;
			}
			else
			{
				bit_offset -= 6;
			}
		}
		if (bit_offset < 18)
		{
			output = aux::host_to_network(output);
			aux::array<char, 4> tmp;
			std::memcpy(tmp.data(), &output, 4);
			ret.push_back(tmp[1]);
			if (bit_offset < 6)
				ret.push_back(tmp[2]);
		}
		return ret;
	}

	std::string base32encode_i2p(span<char const> s)
	{
		static char const base32_table[] =
		{
			'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
			'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
			'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
			'y', 'z', '2', '3', '4', '5', '6', '7'
		};

		static aux::array<int, 6> const input_output_mapping{{{0, 2, 4, 5, 7, 8}}};

		aux::array<std::uint8_t, 5> inbuf;
		aux::array<std::uint8_t, 8> outbuf;

		std::string ret;
		for (auto i = s.begin(); i != s.end();)
		{
			int const available_input = std::min(int(inbuf.size()), int(s.end() - i));

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
				ret += base32_table[outbuf[j]];
			// i2p does not use padding
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
		std::wstring ws;
		ws.resize(s.size() + 1);
		int wsize = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], int(ws.size()));
		if (wsize < 0) return {};
		if (wsize > 0 && ws[wsize - 1] == '\0') --wsize;
		ws.resize(wsize);
		return ws;
	}

	std::string convert_from_wstring(std::wstring const& s)
	{
		std::string ret;
		ret.resize(s.size() * 4 + 1);
		int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1
			, &ret[0], int(ret.size()), nullptr, nullptr);
		if (size < 0) return {};
		if (size > 0 && ret[size - 1] == '\0') --size;
		ret.resize(size);
		return ret;
	}
#endif

#if !TORRENT_NATIVE_UTF8

#if defined TORRENT_WINDOWS

namespace {

	std::string convert_impl(std::string const& s, UINT from, UINT to)
	{
		// if the local codepage is already UTF-8, no need to convert
		static UINT const cp = GetACP();
		if (cp == CP_UTF8) return s;

		std::wstring ws;
		ws.resize(s.size() + 1);
		int wsize = MultiByteToWideChar(from, 0, s.c_str(), -1, &ws[0], int(ws.size()));
		if (wsize > 0 && ws[wsize - 1] == '\0') --wsize;
		ws.resize(wsize);

		std::string ret;
		ret.resize(ws.size() * 4 + 1);
		int size = WideCharToMultiByte(to, 0, ws.c_str(), -1, &ret[0], int(ret.size()), nullptr, nullptr);
		if (size > 0 && ret[size - 1] == '\0') --size;
		ret.resize(size);
		return ret;
	}
} // anonymous namespace

	std::string convert_to_native(std::string const& s)
	{
		return convert_impl(s, CP_UTF8, CP_ACP);
	}

	std::string convert_from_native(std::string const& s)
	{
		return convert_impl(s, CP_ACP, CP_UTF8);
	}

#else

namespace {

	bool ends_with(string_view s, string_view suffix)
	{
		return s.size() >= suffix.size()
			&& s.substr(s.size() - suffix.size()) == suffix;
	}

	bool has_utf8_locale()
	{
		char const* lang = std::getenv("LANG");
		if (lang == nullptr) return false;
		return ends_with(lang, ".UTF-8");
	}

	bool need_conversion()
	{
		static bool const ret = has_utf8_locale();
		return !ret;
	}
}

	std::string convert_to_native(std::string const& s)
	{
		if (!need_conversion()) return s;

		std::mbstate_t state{};
		std::string ret;
		string_view ptr = s;
		while (!ptr.empty())
		{
			std::int32_t codepoint;
			int len;

			// decode a single utf-8 character
			std::tie(codepoint, len) = parse_utf8_codepoint(ptr);

			if (codepoint == -1)
				codepoint = '.';

			ptr = ptr.substr(std::size_t(len));

			char out[10];
			std::size_t const size = std::wcrtomb(out, static_cast<wchar_t>(codepoint), &state);
			if (size == static_cast<std::size_t>(-1))
			{
				ret += '.';
				state = std::mbstate_t{};
			}
			else
				for (std::size_t i = 0; i < size; ++i)
					ret += out[i];
		}
		return ret;
	}

	std::string convert_from_native(std::string const& s)
	{
		if (!need_conversion()) return s;

		std::mbstate_t state{};
		std::string ret;
		string_view ptr = s;
		while (!ptr.empty())
		{
			wchar_t codepoint;
			std::size_t const size = std::mbrtowc(&codepoint, ptr.data(), ptr.size(), &state);
			if (size == static_cast<std::size_t>(-1))
			{
				ret.push_back('.');
				state = std::mbstate_t{};
				ptr = ptr.substr(1);
			}
			else
			{
				append_utf8_codepoint(ret, static_cast<std::int32_t>(codepoint));
				ptr = ptr.substr(size < 1 ? 1 : size);
			}
		}

		return ret;
	}

#endif
#endif

}
