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

#include "libtorrent/hex.hpp"

namespace libtorrent {

	namespace aux {

	int hex_to_int(char in)
	{
		if (in >= '0' && in <= '9') return int(in) - '0';
		if (in >= 'A' && in <= 'F') return int(in) - 'A' + 10;
		if (in >= 'a' && in <= 'f') return int(in) - 'a' + 10;
		return -1;
	}

	bool is_hex(span<char const> in)
	{
		for (char const c : in)
		{
			int const t = hex_to_int(c);
			if (t == -1) return false;
		}
		return true;
	}

	bool from_hex(span<char const> in, char* out)
	{
		for (auto i = in.begin(), end = in.end(); i != end; ++i, ++out)
		{
			int const t1 = aux::hex_to_int(*i);
			if (t1 == -1) return false;
			*out = char(t1 << 4);
			++i;
			int const t2 = aux::hex_to_int(*i);
			if (t2 == -1) return false;
			*out |= t2 & 15;
		}
		return true;
	}

	extern char const hex_chars[];

	char const hex_chars[] = "0123456789abcdef";
	void to_hex(char const* in, int const len, char* out)
	{
		int idx = 0;
		for (int i = 0; i < len; ++i)
		{
			out[idx++] = hex_chars[std::uint8_t(in[i]) >> 4];
			out[idx++] = hex_chars[std::uint8_t(in[i]) & 0xf];
		}
	}

	std::string to_hex(span<char const> in)
	{
		std::string ret;
		if (!in.empty())
		{
			ret.resize(std::size_t(in.size() * 2));
			to_hex(in.data(), int(in.size()), &ret[0]);
		}
		return ret;
	}

	void to_hex(span<char const> in, char* out)
	{
		to_hex(in.data(), int(in.size()), out);
		out[in.size() * 2] = '\0';
	}

	} // aux namespace

}
