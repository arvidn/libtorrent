/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/hex.hpp"

namespace libtorrent
{

	namespace aux {

	int hex_to_int(char in)
	{
		if (in >= '0' && in <= '9') return int(in) - '0';
		if (in >= 'A' && in <= 'F') return int(in) - 'A' + 10;
		if (in >= 'a' && in <= 'f') return int(in) - 'a' + 10;
		return -1;
	}

	bool is_hex(char const *in, int len)
	{
		for (char const* end = in + len; in < end; ++in)
		{
			int t = hex_to_int(*in);
			if (t == -1) return false;
		}
		return true;
	}

	bool from_hex(char const *in, int len, char* out)
	{
		for (char const* end = in + len; in < end; ++in, ++out)
		{
			int t = aux::hex_to_int(*in);
			if (t == -1) return false;
			*out = t << 4;
			++in;
			t = aux::hex_to_int(*in);
			if (t == -1) return false;
			*out |= t & 15;
		}
		return true;
	}

	extern char const hex_chars[];

	char const hex_chars[] = "0123456789abcdef";

	std::string to_hex(std::string const& s)
	{
		std::string ret;
		for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
		{
			ret += hex_chars[std::uint8_t(*i) >> 4];
			ret += hex_chars[std::uint8_t(*i) & 0xf];
		}
		return ret;
	}

	void to_hex(char const *in, int len, char* out)
	{
		for (char const* end = in + len; in < end; ++in)
		{
			*out++ = hex_chars[std::uint8_t(*in) >> 4];
			*out++ = hex_chars[std::uint8_t(*in) & 0xf];
		}
		*out = '\0';
	}

	} // aux namespace

}

