/*

Copyright (c) 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2018, Alexandre Janniaux
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

namespace lt::aux {
namespace {

#if TORRENT_HAS_SSE
	// internal
	void cpuid(std::uint32_t* info, int type) noexcept
	{
#if defined _MSC_VER
		__cpuid(reinterpret_cast<int*>(info), type);

#elif defined __GNUC__
		__get_cpuid(std::uint32_t(type), &info[0], &info[1], &info[2], &info[3]);
#else
		TORRENT_UNUSED(type);
		// for non-x86 and non-amd64, just return zeroes
		std::memset(&info[0], 0, sizeof(std::uint32_t) * 4);
#endif
	}
#endif

	bool supports_sse42() noexcept
	{
#if TORRENT_HAS_SSE
		std::uint32_t cpui[4] = {0};
		cpuid(cpui, 1);
		return (cpui[2] & (1 << 20)) != 0;
#else
		return false;
#endif
	}

	bool supports_mmx() noexcept
	{
#if TORRENT_HAS_SSE
		std::uint32_t cpui[4] = {0};
		cpuid(cpui, 1);
		return (cpui[2] & (1 << 23)) != 0;
#else
		return false;
#endif
	}

	bool supports_arm_neon() noexcept
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

	bool supports_arm_crc32c() noexcept
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
}
