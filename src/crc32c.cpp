/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2018, Pavel Pimenov
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
#include "libtorrent/crc32c.hpp"
#include "libtorrent/aux_/cpuid.hpp"
#include "libtorrent/aux_/byteswap.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/crc.hpp>
#if (defined _MSC_VER && _MSC_VER >= 1600 && (defined _M_IX86 || defined _M_X64))
#include <nmmintrin.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if TORRENT_HAS_ARM_CRC32
#include <arm_acle.h>
#endif

namespace libtorrent {

	std::uint32_t crc32c_32(std::uint32_t v)
	{
#if TORRENT_HAS_SSE
		if (aux::sse42_support)
		{
			std::uint32_t ret = 0xffffffff;
#ifdef __GNUC__
			// we can't use these because then we'd have to tell
			// -msse4.2 to gcc on the command line
//			return __builtin_ia32_crc32si(ret, v) ^ 0xffffffff;
			asm ("crc32l\t" "(%1), %0"
				: "=r"(ret)
				: "r"(&v), "0"(ret));
			return ret ^ 0xffffffff;
#else
			return _mm_crc32_u32(ret, v) ^ 0xffffffff;
#endif
		}
#endif

#if TORRENT_HAS_ARM_CRC32
		if (aux::arm_crc32c_support)
		{
			std::uint32_t ret = 0xffffffff;
			return __crc32cw(ret, v) ^ 0xffffffff;
		}
#endif

		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
		crc.process_bytes(&v, 4);
		return crc.checksum();
	}

	std::uint32_t crc32c(std::uint64_t const* buf, int num_words)
	{
#if TORRENT_HAS_SSE
		if (aux::sse42_support)
		{
#if defined _M_AMD64 || defined __x86_64__ \
	|| defined __x86_64 || defined _M_X64 || defined __amd64__
			std::uint64_t ret = 0xffffffff;
			for (int i = 0; i < num_words; ++i)
			{
#ifdef __GNUC__
				// we can't use these because then we'd have to tell
				// -msse4.2 to gcc on the command line
//				ret = __builtin_ia32_crc32di(ret, buf[i]);
				__asm__("crc32q\t" "(%1), %0"
					: "=r"(ret)
					: "r"(buf+i), "0"(ret));
#else
				ret = _mm_crc32_u64(ret, buf[i]);
#endif
			}
			return std::uint32_t(ret) ^ 0xffffffff;
#else
			std::uint32_t ret = 0xffffffff;
			std::uint32_t const* buf0 = reinterpret_cast<std::uint32_t const*>(buf);
			for (int i = 0; i < num_words; ++i)
			{
#ifdef __GNUC__
				// we can't use these because then we'd have to tell
				// -msse4.2 to gcc on the command line
//				ret = __builtin_ia32_crc32si(ret, buf0[i*2]);
//				ret = __builtin_ia32_crc32si(ret, buf0[i*2+1]);
				asm ("crc32l\t" "(%1), %0"
					: "=r"(ret)
					: "r"(buf0+i*2), "0"(ret));
				asm ("crc32l\t" "(%1), %0"
					: "=r"(ret)
					: "r"(buf0+i*2+1), "0"(ret));
#else
				ret = _mm_crc32_u32(ret, buf0[i*2]);
				ret = _mm_crc32_u32(ret, buf0[i*2+1]);
#endif
			}
			return ret ^ 0xffffffff;
#endif // amd64 or x86
		}
#endif // x86 or amd64 and gcc or msvc

#if TORRENT_HAS_ARM_CRC32
		if (aux::arm_crc32c_support)
		{
			std::uint32_t ret = 0xffffffff;
			for (int i = 0; i < num_words; ++i)
			{
				ret = __crc32cd(ret, buf[i]);
			}
			return ret ^ 0xffffffff;
		}
#endif

		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
		crc.process_bytes(buf, std::size_t(num_words) * 8);
		return crc.checksum();
	}
}
