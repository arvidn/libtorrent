/*

Copyright (c) 2014-2018, Arvid Norberg
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
#include "libtorrent/aux_/cpuid.hpp"

#include <cstdint>

#if defined _MSC_VER && TORRENT_HAS_SSE
#include <intrin.h>
#include <nmmintrin.h>
#endif

#if TORRENT_HAS_SSE && defined __GNUC__
#include <cpuid.h>
#else
#include <cstring> // for std::memset
#endif

#if defined __GLIBC__ && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 16))
#define TORRENT_HAS_AUXV 1
#elif defined TORRENT_ANDROID
#define TORRENT_HAS_AUXV 1
#else
#define TORRENT_HAS_AUXV 0
#endif

#if TORRENT_HAS_ARM && TORRENT_HAS_AUXV
#if defined TORRENT_ANDROID
#include <dlfcn.h>
namespace {
unsigned long int helper_getauxval(unsigned long int type)
{
    using getauxval_t = unsigned long int(*)(unsigned long int);
    getauxval_t pf_getauxval = reinterpret_cast<getauxval_t>(dlsym(RTLD_DEFAULT, "getauxval"));
    if (pf_getauxval == nullptr)
        return 0;
    return pf_getauxval(type);
}
}
#else // TORRENT_ANDROID
#include <sys/auxv.h>
#define helper_getauxval getauxval
#endif
#endif // TORRENT_HAS_ARM && TORRENT_HAS_AUXV

namespace libtorrent { namespace aux {

	namespace {

#if TORRENT_HAS_SSE
	// internal
	void cpuid(std::uint32_t* info, int type)
	{
#if defined _MSC_VER
		__cpuid((int*)info, type);

#elif defined __GNUC__
		__get_cpuid(std::uint32_t(type), &info[0], &info[1], &info[2], &info[3]);
#else
		TORRENT_UNUSED(type);
		// for non-x86 and non-amd64, just return zeroes
		std::memset(&info[0], 0, sizeof(std::uint32_t) * 4);
#endif
	}
#endif

	bool supports_sse42()
	{
#if TORRENT_HAS_SSE
		std::uint32_t cpui[4] = {0};
		cpuid(cpui, 1);
		return (cpui[2] & (1 << 20)) != 0;
#else
		return false;
#endif
	}

	bool supports_mmx()
	{
#if TORRENT_HAS_SSE
		std::uint32_t cpui[4] = {0};
		cpuid(cpui, 1);
		return (cpui[2] & (1 << 23)) != 0;
#else
		return false;
#endif
	}

	bool supports_arm_neon()
	{
#if TORRENT_HAS_ARM_NEON && TORRENT_HAS_AUXV
#if defined __arm__
		//return (getauxval(AT_HWCAP) & HWCAP_NEON);
		return (helper_getauxval(16) & (1 << 12));
#elif defined __aarch64__
		//return (getauxval(AT_HWCAP) & HWCAP_ASIMD);
		//return (getauxval(16) & (1 << 1));
		// TODO: enable when aarch64 is really tested
		return false;
#endif
#else
		return false;
#endif
	}

	bool supports_arm_crc32c()
	{
#if TORRENT_HAS_ARM_CRC32 && TORRENT_HAS_AUXV
#if defined TORRENT_FORCE_ARM_CRC32
		return true;
#elif defined __arm__
		//return (getauxval(AT_HWCAP2) & HWCAP2_CRC32);
		return (helper_getauxval(26) & (1 << 4));
#elif defined __aarch64__
		//return (getauxval(AT_HWCAP) & HWCAP_CRC32);
		return (helper_getauxval(16) & (1 << 7));
#endif
#else
		return false;
#endif
	}

	} // anonymous namespace

	bool const sse42_support = supports_sse42();
	bool const mmx_support = supports_mmx();
	bool const arm_neon_support = supports_arm_neon();
	bool const arm_crc32c_support = supports_arm_crc32c();
} }
