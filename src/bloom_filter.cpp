/*

Copyright (c) 2010-2011, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/bloom_filter.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace lt::aux {

	bool has_bits(std::uint8_t const* k, std::uint8_t const* bits, int const len)
	{
		std::uint32_t idx1 = std::uint32_t(k[0]) | (std::uint32_t(k[1]) << 8);
		std::uint32_t idx2 = std::uint32_t(k[2]) | (std::uint32_t(k[3]) << 8);
		idx1 %= aux::numeric_cast<std::uint32_t>(len * 8);
		idx2 %= aux::numeric_cast<std::uint32_t>(len * 8);
		return (bits[idx1 / 8] & (1 << (idx1 & 7))) != 0
			&& (bits[idx2 / 8] & (1 << (idx2 & 7))) != 0;
	}

	void set_bits(std::uint8_t const* k, std::uint8_t* bits, int const len)
	{
		std::uint32_t idx1 = std::uint32_t(k[0]) | (std::uint32_t(k[1]) << 8);
		std::uint32_t idx2 = std::uint32_t(k[2]) | (std::uint32_t(k[3]) << 8);
		idx1 %= aux::numeric_cast<std::uint32_t>(len * 8);
		idx2 %= aux::numeric_cast<std::uint32_t>(len * 8);
		bits[idx1 / 8] |= (1 << (idx1 & 7));
		bits[idx2 / 8] |= (1 << (idx2 & 7));
	}

	int count_zero_bits(std::uint8_t const* bits, int const len)
	{
		// number of bits _not_ set in a nibble
		std::uint8_t bitcount[16] =
		{
			// 0000, 0001, 0010, 0011, 0100, 0101, 0110, 0111,
			// 1000, 1001, 1010, 1011, 1100, 1101, 1110, 1111
			4, 3, 3, 2, 3, 2, 2, 1,
			3, 2, 2, 1, 2, 1, 1, 0
		};
		int ret = 0;
		for (int i = 0; i < len; ++i)
		{
			ret += bitcount[bits[i] & 0xf];
			ret += bitcount[(bits[i] >> 4) & 0xf];
		}
		return ret;
	}
}
