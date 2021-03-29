/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2018, Pavel Pimenov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/crc32c.hpp"
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

namespace lt::aux {

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
