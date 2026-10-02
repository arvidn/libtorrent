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
#include "libtorrent/aux_/string_util.hpp" // for to_lower
#include "libtorrent/aux_/disable_warnings_push.hpp"

#if (defined _MSC_VER && _MSC_VER >= 1600 && (defined _M_IX86 || defined _M_X64))
#include <nmmintrin.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if TORRENT_HAS_ARM_CRC32
#include <arm_acle.h>
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

// For documentation on GCC's asm blocks, see:
// https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html
// and the input- and output operand constraints specifically, see:
// https://gcc.gnu.org/onlinedocs/gcc/Constraints.html

namespace libtorrent::aux {

namespace {

// each backend mixes 8, 32 or 64 bits into a running crc32c state.
// kept separate from the higher-level functions below so that a loop
// over many mix8/mix64 calls (e.g. hashing a path element) only needs
// to pick a backend once, not on every call.

#if TORRENT_HAS_SSE
struct backend_sse42
{
	static std::uint32_t mix8(std::uint32_t const state, std::uint8_t const b)
	{
#ifdef __GNUC__
		std::uint32_t ret = state;
		asm("crc32b\t%1, %0" : "=r"(ret) : "r"(b), "0"(ret));
		return ret;
#else
		return _mm_crc32_u8(state, b);
#endif
	}

	static std::uint32_t mix32(std::uint32_t const state, std::uint32_t const v)
	{
#ifdef __GNUC__
		std::uint32_t ret = state;
		asm("crc32l\t%1, %0" : "=r"(ret) : "r"(v), "0"(ret));
		return ret;
#else
		return _mm_crc32_u32(state, v);
#endif
	}

	static std::uint32_t mix64(std::uint32_t const state, std::uint64_t const w)
	{
#if defined _M_AMD64 || defined __x86_64__ || defined __x86_64 || defined _M_X64 \
	|| defined __amd64__
#ifdef __GNUC__
		std::uint64_t ret = state;
		asm("crc32q\t%1, %0" : "=r"(ret) : "r"(w), "0"(ret));
		return std::uint32_t(ret);
#else
		return std::uint32_t(_mm_crc32_u64(state, w));
#endif
#else
		std::uint32_t ret = mix32(state, std::uint32_t(w));
		ret = mix32(ret, std::uint32_t(w >> 32));
		return ret;
#endif
	}
};
#endif

#if TORRENT_HAS_ARM_CRC32
struct backend_arm
{
	static std::uint32_t mix8(std::uint32_t const state, std::uint8_t const b)
	{
		return __crc32cb(state, b);
	}
	static std::uint32_t mix32(std::uint32_t const state, std::uint32_t const v)
	{
		return __crc32cw(state, v);
	}
	static std::uint32_t mix64(std::uint32_t const state, std::uint64_t const w)
	{
		return __crc32cd(state, w);
	}
};
#endif

struct backend_software
{
	static std::uint32_t mix8(std::uint32_t const state, std::uint8_t const b)
	{
		// bit-serial update using the bit-reversed form of the crc32c
		// polynomial, for this reflected variant
		std::uint32_t crc = state ^ b;
		for (int i = 0; i < 8; ++i)
			crc = (crc & 1) ? (crc >> 1) ^ 0x82f63b78 : (crc >> 1);
		return crc;
	}

	static std::uint32_t mix32(std::uint32_t state, std::uint32_t const v)
	{
		state = mix8(state, std::uint8_t(v));
		state = mix8(state, std::uint8_t(v >> 8));
		state = mix8(state, std::uint8_t(v >> 16));
		state = mix8(state, std::uint8_t(v >> 24));
		return state;
	}

	static std::uint32_t mix64(std::uint32_t state, std::uint64_t const w)
	{
		for (int i = 0; i < 8; ++i)
			state = mix8(state, std::uint8_t(w >> (8 * i)));
		return state;
	}
};

template <class Backend, class Transform>
std::uint32_t mix_string_impl(std::uint32_t state, string_view const str, Transform const t)
{
	std::size_t i = 0;
	std::size_t const n = str.size();
	for (; i + 8 <= n; i += 8)
	{
		std::uint64_t word = 0;
		for (int j = 0; j < 8; ++j)
			word |= std::uint64_t(std::uint8_t(t(str[i + std::size_t(j)]))) << (8 * j);
		state = Backend::mix64(state, word);
	}
	for (; i < n; ++i)
		state = Backend::mix8(state, std::uint8_t(t(str[i])));
	return state;
}

template <class Backend>
std::uint32_t mix_lowercase_impl(std::uint32_t const state, string_view const str)
{
	return mix_string_impl<Backend>(state, str, [](char const c) { return aux::to_lower(c); });
}

template <class Backend>
std::uint32_t mix_impl(std::uint32_t const state, string_view const str)
{
	return mix_string_impl<Backend>(state, str, [](char const c) { return c; });
}

template <class Backend>
std::uint32_t mix_array_impl(std::uint32_t state, std::array<std::uint64_t, 4> const& v)
{
	for (std::uint64_t const w : v)
		state = Backend::mix64(state, w);
	return state;
}

} // anonymous namespace

std::uint32_t crc32c(std::uint32_t const v)
{
#if TORRENT_HAS_SSE
	if (aux::sse42_support)
		return crc32c_finish(backend_sse42::mix32(crc32c_init, v));
#endif
#if TORRENT_HAS_ARM_CRC32
	if (aux::arm_crc32c_support)
		return crc32c_finish(backend_arm::mix32(crc32c_init, v));
#endif
	return crc32c_finish(backend_software::mix32(crc32c_init, v));
}

std::uint32_t crc32c(std::uint64_t const v)
{
#if TORRENT_HAS_SSE
	if (aux::sse42_support)
		return crc32c_finish(backend_sse42::mix64(crc32c_init, v));
#endif
#if TORRENT_HAS_ARM_CRC32
	if (aux::arm_crc32c_support)
		return crc32c_finish(backend_arm::mix64(crc32c_init, v));
#endif
	return crc32c_finish(backend_software::mix64(crc32c_init, v));
}

std::uint32_t crc32c(std::array<std::uint64_t, 4> const& v)
{
#if TORRENT_HAS_SSE
	if (aux::sse42_support)
		return crc32c_finish(mix_array_impl<backend_sse42>(crc32c_init, v));
#endif
#if TORRENT_HAS_ARM_CRC32
	if (aux::arm_crc32c_support)
		return crc32c_finish(mix_array_impl<backend_arm>(crc32c_init, v));
#endif
	return crc32c_finish(mix_array_impl<backend_software>(crc32c_init, v));
}

std::uint32_t crc32c_mix(std::uint32_t const state, std::uint8_t const b)
{
#if TORRENT_HAS_SSE
	if (aux::sse42_support)
		return backend_sse42::mix8(state, b);
#endif
#if TORRENT_HAS_ARM_CRC32
	if (aux::arm_crc32c_support)
		return backend_arm::mix8(state, b);
#endif
	return backend_software::mix8(state, b);
}

std::uint32_t crc32c_mix(std::uint32_t const state, string_view const str)
{
#if TORRENT_HAS_SSE
	if (aux::sse42_support)
		return mix_impl<backend_sse42>(state, str);
#endif
#if TORRENT_HAS_ARM_CRC32
	if (aux::arm_crc32c_support)
		return mix_impl<backend_arm>(state, str);
#endif
	return mix_impl<backend_software>(state, str);
}

std::uint32_t crc32c_mix_lowercase(std::uint32_t const state, string_view const str)
{
#if TORRENT_HAS_SSE
	if (aux::sse42_support)
		return mix_lowercase_impl<backend_sse42>(state, str);
#endif
#if TORRENT_HAS_ARM_CRC32
	if (aux::arm_crc32c_support)
		return mix_lowercase_impl<backend_arm>(state, str);
#endif
	return mix_lowercase_impl<backend_software>(state, str);
}
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
