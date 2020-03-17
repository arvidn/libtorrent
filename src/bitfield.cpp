/*

Copyright (c) 2008-2018, Arvid Norberg
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

#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/cpuid.hpp"

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace libtorrent {

	bool bitfield::all_set() const noexcept
	{
		if(size() == 0) return false;

		int const words = size() / 32;
		for (int i = 1; i < words + 1; ++i)
		{
			if (m_buf[i] != 0xffffffff) return false;
		}
		int const rest = size() & 31;
		if (rest > 0)
		{
			std::uint32_t const mask = aux::host_to_network(0xffffffff << (32 - rest));
			if ((m_buf[words + 1] & mask) != mask) return false;
		}
		return true;
	}

	int bitfield::count() const noexcept
	{
		int ret = 0;
		int const words = num_words();
#if TORRENT_HAS_SSE
		if (aux::mmx_support)
		{
			for (int i = 1; i < words + 1; ++i)
			{
#ifdef __GNUC__
				std::uint32_t cnt = 0;
				__asm__("popcnt %1, %0"
					: "=r"(cnt)
					: "r"(m_buf[i]));
				ret += cnt;
#else
				ret += _mm_popcnt_u32(m_buf[i]);
#endif
			}

			TORRENT_ASSERT(ret <= size());
			TORRENT_ASSERT(ret >= 0);
			return ret;
		}
#endif // TORRENT_HAS_SSE

#if TORRENT_HAS_ARM_NEON && defined __arm__
		if (aux::arm_neon_support)
		{
			for (int i = 1; i < words + 1; ++i)
			{
				std::uint32_t cnt;
				__asm__(
					"vld1.u32 d0[0], [%1] \n"
					"vcnt.u8 d0, d0 \n"
					"vpaddl.u8 d0, d0 \n"
					"vpaddl.u16 d0, d0 \n"
					"vst1.u32 d0[0], [%0]"
					:: "r"(&cnt), "r"(&m_buf[i])
					: "d0", "memory");
				ret += cnt;
			}

			TORRENT_ASSERT(ret <= size());
			TORRENT_ASSERT(ret >= 0);
			return ret;
		}
#endif // TORRENT_HAS_ARM_NEON

		for (int i = 1; i < words + 1; ++i)
		{
			std::uint32_t const v = m_buf[i];
			// from:
			// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
			static const int S[] = {1, 2, 4, 8, 16}; // Magic Binary Numbers
			static const std::uint32_t B[] = {0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF};

			std::uint32_t c = v - ((v >> 1) & B[0]);
			c = ((c >> S[1]) & B[1]) + (c & B[1]);
			c = ((c >> S[2]) + c) & B[2];
			c = ((c >> S[3]) + c) & B[3];
			c = ((c >> S[4]) + c) & B[4];
			ret += c;
			TORRENT_ASSERT(ret <= size());
		}

		TORRENT_ASSERT(ret <= size());
		TORRENT_ASSERT(ret >= 0);
		return ret;
	}

	void bitfield::resize(int const bits, bool const val)
	{
		if (bits == size()) return;

		int const s = size();
		int const b = size() & 31;
		resize(bits);
		if (s >= size()) return;
		int const old_size_words = (s + 31) / 32;
		int const new_size_words = num_words();
		if (val)
		{
			if (old_size_words && b) buf()[old_size_words - 1] |= aux::host_to_network(0xffffffff >> b);
			if (old_size_words < new_size_words)
				std::memset(buf() + old_size_words, 0xff
					, static_cast<std::size_t>(new_size_words - old_size_words) * 4);
			clear_trailing_bits();
		}
		else
		{
			if (old_size_words < new_size_words)
				std::memset(buf() + old_size_words, 0x00
					, static_cast<std::size_t>(new_size_words - old_size_words) * 4);
		}
		TORRENT_ASSERT(size() == bits);
	}

	void bitfield::resize(int const bits)
	{
		if (bits == size()) return;

		TORRENT_ASSERT(bits >= 0);
		if (bits == 0)
		{
			m_buf.reset();
			return;
		}
		int const new_size_words = (bits + 31) / 32;
		int const cur_size_words = num_words();
		if (cur_size_words != new_size_words)
		{
			aux::unique_ptr<std::uint32_t[]> b(new std::uint32_t[std::size_t(new_size_words + 1)]);
#ifdef BOOST_NO_EXCEPTIONS
			if (b == nullptr) std::terminate();
#endif
			b[0] = aux::numeric_cast<std::uint32_t>(bits);
			if (m_buf) std::memcpy(&b[1], buf()
				, aux::numeric_cast<std::size_t>(std::min(new_size_words, cur_size_words) * 4));
			if (new_size_words > cur_size_words)
			{
				std::memset(&b[1 + cur_size_words], 0
					, aux::numeric_cast<std::size_t>((new_size_words - cur_size_words) * 4));
			}
			m_buf = std::move(b);
		}
		else
		{
			m_buf[0] = aux::numeric_cast<std::uint32_t>(bits);
		}

		clear_trailing_bits();
		TORRENT_ASSERT(size() == bits);
	}

	int bitfield::find_first_set() const noexcept
	{
		int const num = num_words();
		if (num == 0) return -1;
		int const count = aux::count_leading_zeros({&m_buf[1], num});
		return count != num * 32 ? count : -1;
	}

	int bitfield::find_last_clear() const noexcept
	{
		int const num = num_words();
		if (num == 0) return - 1;
		int const size = this->size();
		std::uint32_t const mask = 0xffffffff << (32 - (size & 31));
		std::uint32_t const last = m_buf[num] ^ aux::host_to_network(mask);
		int const ext = aux::count_trailing_ones(~last) - (31 - (size % 32));
		return last != 0
			? (num - 1) * 32 + ext
			: size - (aux::count_trailing_ones({&m_buf[1], num - 1}) + ext);
	}

	static_assert(std::is_nothrow_move_constructible<bitfield>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<bitfield>::value
		, "should be nothrow move assignable");
	static_assert(std::is_nothrow_default_constructible<bitfield>::value
		, "should be nothrow default constructible");

	static_assert(std::is_nothrow_move_constructible<typed_bitfield<int>>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<typed_bitfield<int>>::value
		, "should be nothrow move assignable");
	static_assert(std::is_nothrow_default_constructible<typed_bitfield<int>>::value
		, "should be nothrow default constructible");
}
