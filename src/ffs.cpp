/*

Copyright (c) 2014-2016, Arvid Norberg, Alden Torres
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
#include "libtorrent/aux_/ffs.hpp"
#include "libtorrent/aux_/byteswap.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if (defined _MSC_VER && _MSC_VER >= 1600 && (defined _M_IX86 || defined _M_X64))
#include <nmmintrin.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {
namespace aux {

	// returns the index of the first set bit.
	// Use std::log2p1 in C++20
	int log2p1(std::uint32_t v)
	{
// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
		static const int MultiplyDeBruijnBitPosition[32] =
		{
			0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
			8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
		};

		v |= v >> 1; // first round down to one less than a power of 2
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;

		return MultiplyDeBruijnBitPosition[std::uint32_t(v * 0x07C4ACDDU) >> 27];
	}

	int count_leading_zeros_sw(span<std::uint32_t const> buf)
	{
		auto const num = int(buf.size());
		std::uint32_t const* ptr = buf.data();

		TORRENT_ASSERT(num >= 0);
		TORRENT_ASSERT(ptr != nullptr);

		for (int i = 0; i < num; i++)
		{
			if (ptr[i] == 0) continue;
			return i * 32 + 31 - log2p1(aux::network_to_host(ptr[i]));
		}

		return num * 32;
	}

	int count_leading_zeros_hw(span<std::uint32_t const> buf)
	{
		auto const num = int(buf.size());
		std::uint32_t const* ptr = buf.data();

		TORRENT_ASSERT(num >= 0);
		TORRENT_ASSERT(ptr != nullptr);

		for (int i = 0; i < num; i++)
		{
			if (ptr[i] == 0) continue;

#if TORRENT_HAS_BUILTIN_CLZ
			std::uint32_t const v = aux::network_to_host(ptr[i]);
			return i * 32 + __builtin_clz(v);
#elif defined _MSC_VER
			std::uint32_t const v = aux::network_to_host(ptr[i]);
			DWORD pos;
			_BitScanReverse(&pos, v);
			return i * 32 + 31 - pos;
#else
			TORRENT_ASSERT_FAIL();
			return -1;
#endif
		}

		return num * 32;
	}

	int count_leading_zeros(span<std::uint32_t const> buf)
	{
#if TORRENT_HAS_BUILTIN_CLZ || defined _MSC_VER
		return aux::count_leading_zeros_hw(buf);
#else
		return aux::count_leading_zeros_sw(buf);
#endif
	}

	int count_trailing_ones_sw(span<std::uint32_t const> buf)
	{
		auto const num = int(buf.size());
		std::uint32_t const* ptr = buf.data();

		TORRENT_ASSERT(num >= 0);
		TORRENT_ASSERT(ptr != nullptr);

		for (int i = num - 1; i >= 0; i--)
		{
			if (ptr[i] == 0xffffffff) continue;
			std::uint32_t v = ~aux::network_to_host(ptr[i]);

			for (int k = 0; k < 32; ++k, v >>= 1)
			{
				if ((v & 1) == 0) continue;
				return (num - i - 1) * 32 + k;
			}
		}

		return num * 32;
	}

	int count_trailing_ones_hw(span<std::uint32_t const> buf)
	{
		auto const num = int(buf.size());
		std::uint32_t const* ptr = buf.data();

		TORRENT_ASSERT(num >= 0);
		TORRENT_ASSERT(ptr != nullptr);

		for (int i = num - 1; i >= 0; i--)
		{
			if (ptr[i] == 0xffffffff) continue;

#if TORRENT_HAS_BUILTIN_CTZ
			std::uint32_t const v = ~aux::network_to_host(ptr[i]);
			return (num - i - 1) * 32 + __builtin_ctz(v);
#elif defined _MSC_VER
			std::uint32_t const v = ~aux::network_to_host(ptr[i]);
			DWORD pos;
			_BitScanForward(&pos, v);
			return (num - i - 1) * 32 + pos;
#else
			TORRENT_ASSERT_FAIL();
			return -1;
#endif
		}

		return num * 32;
	}

	int count_trailing_ones(span<std::uint32_t const> buf)
	{
#if TORRENT_HAS_BUILTIN_CTZ || defined _MSC_VER
		return aux::count_trailing_ones_hw(buf);
#else
		return aux::count_trailing_ones_sw(buf);
#endif
	}
}}
