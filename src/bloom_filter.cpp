/*

Copyright (c) 2010-2016, Arvid Norberg
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

#include "libtorrent/bloom_filter.hpp"

namespace libtorrent
{
	bool has_bits(boost::uint8_t const* k, boost::uint8_t const* bits, int len)
	{
		boost::uint32_t idx1 = boost::uint32_t(k[0]) | (boost::uint32_t(k[1]) << 8);
		boost::uint32_t idx2 = boost::uint32_t(k[2]) | (boost::uint32_t(k[3]) << 8);
		idx1 %= len * 8;
		idx2 %= len * 8;
		return (bits[idx1/8] & (1 << (idx1 & 7))) != 0
			&& (bits[idx2/8] & (1 << (idx2 & 7))) != 0;
	}

	void set_bits(boost::uint8_t const* k, boost::uint8_t* bits, int len)
	{
		boost::uint32_t idx1 = boost::uint32_t(k[0]) | (boost::uint32_t(k[1]) << 8);
		boost::uint32_t idx2 = boost::uint32_t(k[2]) | (boost::uint32_t(k[3]) << 8);
		idx1 %= len * 8;
		idx2 %= len * 8;
		bits[idx1/8] |= (1 << (idx1 & 7));
		bits[idx2/8] |= (1 << (idx2 & 7));
	}

	int count_zero_bits(boost::uint8_t const* bits, int len)
	{
		// number of bits _not_ set in a nibble
		boost::uint8_t bitcount[16] =
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

