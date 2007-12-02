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
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

#include <boost/optional.hpp>

#include "libtorrent/assert.hpp"

namespace libtorrent
{
	std::string unescape_string(std::string const& s)
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
					throw std::runtime_error("invalid escaped string");

				int high;
				if(*i >= '0' && *i <= '9') high = *i - '0';
				else if(*i >= 'A' && *i <= 'F') high = *i + 10 - 'A';
				else if(*i >= 'a' && *i <= 'f') high = *i + 10 - 'a';
				else throw std::runtime_error("invalid escaped string");

				++i;
				if (i == s.end())
					throw std::runtime_error("invalid escaped string");

				int low;
				if(*i >= '0' && *i <= '9') low = *i - '0';
				else if(*i >= 'A' && *i <= 'F') low = *i + 10 - 'A';
				else if(*i >= 'a' && *i <= 'f') low = *i + 10 - 'a';
				else throw std::runtime_error("invalid escaped string");

				ret += char(high * 16 + low);
			}
		}
		return ret;
	}

	std::string escape_string(const char* str, int len)
	{
		TORRENT_ASSERT(str != 0);
		TORRENT_ASSERT(len >= 0);
		// http://www.ietf.org/rfc/rfc2396.txt
		// section 2.3
		// some trackers seems to require that ' is escaped
//		static const char unreserved_chars[] = "-_.!~*'()";
		static const char unreserved_chars[] = "-_.!~*()"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
			"0123456789";

		std::stringstream ret;
		ret << std::hex << std::setfill('0');
		for (int i = 0; i < len; ++i)
		{
			if (std::count(
					unreserved_chars
					, unreserved_chars+sizeof(unreserved_chars)-1
					, *str))
			{
				ret << *str;
			}
			else
			{
				ret << '%'
					<< std::setw(2)
					<< (int)static_cast<unsigned char>(*str);
			}
			++str;
		}
		return ret.str();
	}
	
	std::string escape_path(const char* str, int len)
	{
		TORRENT_ASSERT(str != 0);
		TORRENT_ASSERT(len >= 0);
		static const char unreserved_chars[] = "/-_.!~*()"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
			"0123456789";

		std::stringstream ret;
		ret << std::hex << std::setfill('0');
		for (int i = 0; i < len; ++i)
		{
			if (std::count(
					unreserved_chars
					, unreserved_chars+sizeof(unreserved_chars)-1
					, *str))
			{
				ret << *str;
			}
			else
			{
				ret << '%'
					<< std::setw(2)
					<< (int)static_cast<unsigned char>(*str);
			}
			++str;
		}
		return ret.str();
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
			int available_input = (std::min)(3, (int)std::distance(i, s.end()));

			// clear input buffer
			std::fill(inbuf, inbuf+3, 0);

			// read a chunk of input into inbuf
			for (int j = 0; j < available_input; ++j)
			{
				inbuf[j] = *i;
				++i;
			}

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
			// available input is 1,2 or 3 bytes
			// since we read 3 bytes at a time at most
			int available_input = (std::min)(5, (int)std::distance(i, s.end()));

			// clear input buffer
			std::fill(inbuf, inbuf+5, 0);

			// read a chunk of input into inbuf
			for (int j = 0; j < available_input; ++j)
			{
				inbuf[j] = *i;
				++i;
			}

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

	boost::optional<std::string> url_has_argument(
		std::string const& url, std::string argument)
	{
		size_t i = url.find('?');
		if (i == std::string::npos) return boost::optional<std::string>();
		++i;

		argument += '=';

		if (url.compare(i, argument.size(), argument) == 0)
		{
			size_t pos = i + argument.size();
			return url.substr(pos, url.find('&', pos) - pos);
		}
		argument.insert(0, "&");
		i = url.find(argument, i);
		if (i == std::string::npos) return boost::optional<std::string>();
		size_t pos = i + argument.size();
		return url.substr(pos, url.find('&', pos) - pos);
	}

}

