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

#include <string>
#include <cassert>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

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
		assert(str != 0);
		assert(len >= 0);
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
		assert(str != 0);
		assert(len >= 0);
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
}
