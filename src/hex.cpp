/*

Copyright (c) 2015-2021, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2016, Pavel Pimenov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/hex.hpp"
#include "libtorrent/aux_/array.hpp"

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
			if (i == end) return false;
			int const t2 = aux::hex_to_int(*i);
			if (t2 == -1) return false;
			*out |= t2 & 15;
		}
		return true;
	}

	extern aux::array<char, 16> const hex_chars;

	aux::array<char, 16> const hex_chars{{
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	}};
	void to_hex(span<char const> in, char* out)
	{
		for (char const c : in)
		{
			*out++ = hex_chars[std::uint8_t(c) >> 4];
			*out++ = hex_chars[std::uint8_t(c) & 0xf];
		}
	}

	std::string to_hex(span<char const> in)
	{
		std::string ret(std::size_t(in.size() * 2), '\0');
		to_hex(in, ret.data());
		return ret;
	}

	} // aux namespace

}
