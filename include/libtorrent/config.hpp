/*

Copyright (c) 2005-2018, Arvid Norberg
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

#ifndef TORRENT_CONFIG_HPP_INCLUDED
#define TORRENT_CONFIG_HPP_INCLUDED

#include "libtorrent/aux_/disable_warnings_push.hpp"

#define _FILE_OFFSET_BITS 64

#include <boost/config.hpp>
#include <boost/version.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

// TODO: don't include that here. Make each header that use the export macros
// include it instead. and move it to aux_
#include "libtorrent/aux_/export.hpp"

#ifdef __linux__
#include <linux/version.h> // for LINUX_VERSION_CODE and KERNEL_VERSION
#endif // __linux

#if defined __MINGW64__ || defined __MINGW32__
// GCC warns on format codes that are incompatible with glibc, which the windows
// format codes are. So we need to disable those for mingw targets
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#endif

#if defined __GNUC__

#ifdef _GLIBCXX_CONCEPT_CHECKS
#define TORRENT_COMPLETE_TYPES_REQUIRED 1
#endif

// ======= SUNPRO =========

#elif defined __SUNPRO_CC

#define TORRENT_COMPLETE_TYPES_REQUIRED 1

// ======= MSVC =========

#elif defined BOOST_MSVC

// class X needs to have dll-interface to be used by clients of class Y
#pragma warning(disable:4251)

// auto and decltype(auto) return types supports since MSVS2015
// https://msdn.microsoft.com/en-us/library/hh567368.aspx
// we need to force C++14 feature due VS2017 inability to parse C++11 syntax
#if defined(_MSC_VER) && (_MSC_VER > 1900)
#define TORRENT_AUTO_RETURN_TYPES 1
#endif
#endif


// ======= PLATFORMS =========


// set up defines for target environments
// ==== AMIGA ===
#if defined __AMIGA__ || defined __amigaos__ || defined __AROS__
#define TORRENT_AMIGA
#define TORRENT_USE_IOSTREAM 0
// set this to 1 to disable all floating point operations
// (disables some float-dependent APIs)
#define TORRENT_NO_FPU 1
#define TORRENT_USE_I2P 0
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#endif

// ==== Darwin/BSD ===
#elif (defined __APPLE__ && defined __MACH__) || defined __FreeBSD__ || defined __NetBSD__ \
	|| defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__ \
	|| defined __FreeBSD_kernel__
#define TORRENT_BSD
// we don't need iconv on mac, because
// the locale is always utf-8
#if defined __APPLE__

# ifndef TORRENT_USE_ICONV
#  define TORRENT_USE_ICONV 0
#  define TORRENT_USE_LOCALE 0
# endif
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
// on OSX, use the built-in common crypto for built-in
# if !defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_LIBGCRYPT
#  define TORRENT_USE_COMMONCRYPTO 1
# endif // TORRENT_USE_OPENSSL
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

// execinfo.h is available in the MacOS X 10.5 SDK.
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
#define TORRENT_USE_EXECINFO 1
#endif

#define TORRENT_USE_SYSTEMCONFIGURATION 1

#if TARGET_OS_IPHONE
#define TORRENT_USE_SC_NETWORK_REACHABILITY 1
#endif
#endif // __APPLE__

#define TORRENT_HAS_SYMLINK 1
#define TORRENT_USE_DEV_RANDOM 1
#define TORRENT_HAVE_MMAP 1

#define TORRENT_HAS_FALLOCATE 0

#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_SYSCTL 1
#define TORRENT_USE_IFCONF 1


// ==== LINUX ===
#elif defined __linux__
#define TORRENT_LINUX

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30) && !defined __ANDROID__
# define TORRENT_USE_PREADV 1
# define TORRENT_USE_PREAD 0
#else
# define TORRENT_USE_PREADV 0
# define TORRENT_USE_PREAD 1
#endif

#define TORRENT_HAS_SYMLINK 1
#define TORRENT_HAVE_MMAP 1
#define TORRENT_USE_NETLINK 1
#define TORRENT_USE_IFADDRS 0
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SALEN 0
#define TORRENT_USE_FDATASYNC 1

// ===== ANDROID ===== (almost linux, sort of)
#if defined __ANDROID__
#define TORRENT_ANDROID
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_ICONV 0
#else // ANDROID

// posix_fallocate() is available under this condition
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
#define TORRENT_HAS_FALLOCATE 1
#else
#define TORRENT_HAS_FALLOCATE 0
#endif

#endif // ANDROID

#if defined __GLIBC__ && ( defined __x86_64__ || defined __i386 \
	|| defined _M_X64 || defined _M_IX86 )
#define TORRENT_USE_EXECINFO 1
#endif

// ==== MINGW ===
#elif defined __MINGW32__ || defined __MINGW64__
#define TORRENT_MINGW
#define TORRENT_WINDOWS
#ifndef TORRENT_USE_ICONV
# define TORRENT_USE_ICONV 0
# define TORRENT_USE_LOCALE 1
#endif
#define TORRENT_USE_RLIMIT 0
#define TORRENT_USE_NETLINK 0
#define TORRENT_USE_GETADAPTERSADDRESSES 1
#define TORRENT_HAS_SALEN 0
#define TORRENT_USE_GETIPFORWARDTABLE 1
#define TORRENT_USE_UNC_PATHS 1
// these are emulated on windows
#define TORRENT_USE_PREADV 1
#define TORRENT_USE_PWRITEV 1

// mingw doesn't implement random_device.
#define TORRENT_BROKEN_RANDOM_DEVICE 1

# if !defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_LIBGCRYPT
// unless some other crypto library has been specified, default to the native
// windows CryptoAPI
#define TORRENT_USE_CRYPTOAPI 1

#ifdef NTDDI_VERSION
# if (NTDDI_VERSION > NTDDI_WINXPSP2)
#  define TORRENT_USE_CRYPTOAPI_SHA_512 1
# endif
#else // NTDDI_VERSION not defined so use simple _WIN32_WINNT check
# if _WIN32_WINNT >= 0x0600
#  define TORRENT_USE_CRYPTOAPI_SHA_512 1
# endif
#endif

#endif
// ==== WINDOWS ===
#elif defined _WIN32
#define TORRENT_WINDOWS
#ifndef TORRENT_USE_GETIPFORWARDTABLE
# define TORRENT_USE_GETIPFORWARDTABLE 1
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

# if !defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_LIBGCRYPT
// unless some other crypto library has been specified, default to the native
// windows CryptoAPI
#define TORRENT_USE_CRYPTOAPI 1

#ifdef NTDDI_VERSION
# if (NTDDI_VERSION > NTDDI_WINXPSP2)
#  define TORRENT_USE_CRYPTOAPI_SHA_512 1
# endif
#else // NTDDI_VERSION not defined so use simple _WIN32_WINNT check
# if _WIN32_WINNT >= 0x0600
#  define TORRENT_USE_CRYPTOAPI_SHA_512 1
# endif
#endif

#endif

#define TORRENT_USE_GETADAPTERSADDRESSES 1
#define TORRENT_HAS_SALEN 0
// windows has its own functions to convert
#ifndef TORRENT_USE_ICONV
# define TORRENT_USE_ICONV 0
# define TORRENT_USE_LOCALE 1
#endif
#define TORRENT_USE_RLIMIT 0
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_UNC_PATHS 1
// these are emulated on windows
#define TORRENT_USE_PREADV 1
#define TORRENT_USE_PWRITEV 1

// ==== WINRT ===
#if defined(WINAPI_FAMILY_PARTITION)
# if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) \
  && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#  define TORRENT_WINRT
# endif
#endif

// ==== SOLARIS ===
#elif defined sun || defined __sun
#define TORRENT_SOLARIS
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SALEN 0
#define TORRENT_HAVE_MMAP 1
#define TORRENT_HAS_SYMLINK 1

// ==== BEOS ===
#elif defined __BEOS__ || defined __HAIKU__
#define TORRENT_BEOS
#include <storage/StorageDefs.h> // B_PATH_NAME_LENGTH
#define TORRENT_HAS_FALLOCATE 0
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#endif

// ==== GNU/Hurd ===
#elif defined __GNU__
#define TORRENT_HURD
#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SYMLINK 1

// ==== eCS(OS/2) ===
#elif defined __OS2__
#define TORRENT_OS2
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_IFCONF 1
#define TORRENT_USE_SYSCTL 1
#define TORRENT_USE_WRITEV 0
#define TORRENT_USE_READV 0

#else

#ifdef _MSC_VER
#pragma message ( "unknown OS, assuming BSD" )
#else
#warning "unknown OS, assuming BSD"
#endif

#define TORRENT_BSD
#endif

#define TORRENT_UNUSED(x) (void)(x)

#if defined __GNUC__ || defined __clang__
#define TORRENT_FORMAT(fmt, ellipsis) __attribute__((__format__(__printf__, fmt, ellipsis)))
#else
#define TORRENT_FORMAT(fmt, ellipsis)
#endif

#ifndef TORRENT_BROKEN_RANDOM_DEVICE
#define TORRENT_BROKEN_RANDOM_DEVICE 0
#endif

// libiconv presence detection is not implemented yet
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 1
#endif

#ifndef TORRENT_HAS_SALEN
#define TORRENT_HAS_SALEN 1
#endif

#ifndef TORRENT_USE_GETADAPTERSADDRESSES
#define TORRENT_USE_GETADAPTERSADDRESSES 0
#endif

#ifndef TORRENT_USE_NETLINK
#define TORRENT_USE_NETLINK 0
#endif

#ifndef TORRENT_USE_EXECINFO
#define TORRENT_USE_EXECINFO 0
#endif

#ifndef TORRENT_USE_SYSCTL
#define TORRENT_USE_SYSCTL 0
#endif

#ifndef TORRENT_USE_GETIPFORWARDTABLE
#define TORRENT_USE_GETIPFORWARDTABLE 0
#endif

#ifndef TORRENT_USE_LOCALE
#define TORRENT_USE_LOCALE 0
#endif

#if defined BOOST_NO_STD_WSTRING
#error your C++ standard library appears to be missing std::wstring. This type is required on windows
#endif

#ifndef TORRENT_HAS_FALLOCATE
#define TORRENT_HAS_FALLOCATE 1
#endif

#ifndef TORRENT_USE_COMMONCRYPTO
#define TORRENT_USE_COMMONCRYPTO 0
#endif

#ifndef TORRENT_USE_SYSTEMCONFIGURATION
#define TORRENT_USE_SYSTEMCONFIGURATION 0
#endif

#ifndef TORRENT_USE_SC_NETWORK_REACHABILITY
#define TORRENT_USE_SC_NETWORK_REACHABILITY 0
#endif

#ifndef TORRENT_USE_CRYPTOAPI
#define TORRENT_USE_CRYPTOAPI 0
#endif

#ifndef TORRENT_USE_CRYPTOAPI_SHA_512
#define TORRENT_USE_CRYPTOAPI_SHA_512 0
#endif

#ifndef TORRENT_USE_DEV_RANDOM
#define TORRENT_USE_DEV_RANDOM 0
#endif

#ifndef TORRENT_HAVE_MMAP
#define TORRENT_HAVE_MMAP 0
#endif

#ifndef TORRENT_COMPLETE_TYPES_REQUIRED
#define TORRENT_COMPLETE_TYPES_REQUIRED 0
#endif

#ifndef TORRENT_USE_FDATASYNC
#define TORRENT_USE_FDATASYNC 0
#endif

#ifndef TORRENT_USE_UNC_PATHS
#define TORRENT_USE_UNC_PATHS 0
#endif

#ifndef TORRENT_USE_RLIMIT
#define TORRENT_USE_RLIMIT 1
#endif

#ifndef TORRENT_USE_IFADDRS
#define TORRENT_USE_IFADDRS 0
#endif

// if preadv() exists, we assume pwritev() does as well
#ifndef TORRENT_USE_PREADV
#define TORRENT_USE_PREADV 0
#endif

// if pread() exists, we assume pwrite() does as well
#ifndef TORRENT_USE_PREAD
#define TORRENT_USE_PREAD 1
#endif

#ifndef TORRENT_NO_FPU
#define TORRENT_NO_FPU 0
#endif

#ifndef TORRENT_USE_IOSTREAM
#ifndef BOOST_NO_IOSTREAM
#define TORRENT_USE_IOSTREAM 1
#else
#define TORRENT_USE_IOSTREAM 0
#endif
#endif

#ifndef TORRENT_USE_I2P
#define TORRENT_USE_I2P 1
#endif

#ifndef TORRENT_AUTO_RETURN_TYPES
#define TORRENT_AUTO_RETURN_TYPES 0
#endif

#if !defined(TORRENT_READ_HANDLER_MAX_SIZE)
# if defined _GLIBCXX_DEBUG || !defined NDEBUG
// internal
constexpr std::size_t TORRENT_READ_HANDLER_MAX_SIZE = 432;
# else
// internal
// if this is not divisible by 8, we're wasting space
constexpr std::size_t TORRENT_READ_HANDLER_MAX_SIZE = 342;
# endif
#endif

#if !defined(TORRENT_WRITE_HANDLER_MAX_SIZE)
# if defined _GLIBCXX_DEBUG || !defined NDEBUG
// internal
constexpr std::size_t TORRENT_WRITE_HANDLER_MAX_SIZE = 432;
# else
// internal
// if this is not divisible by 8, we're wasting space
constexpr std::size_t TORRENT_WRITE_HANDLER_MAX_SIZE = 342;
# endif
#endif

#ifndef TORRENT_HAS_SYMLINK
#define TORRENT_HAS_SYMLINK 0
#endif

// debug builds have asserts enabled by default, release
// builds have asserts if they are explicitly enabled by
// the release_asserts macro.
#ifndef TORRENT_USE_ASSERTS
#define TORRENT_USE_ASSERTS 0
#endif // TORRENT_USE_ASSERTS

#ifndef TORRENT_USE_INVARIANT_CHECKS
#define TORRENT_USE_INVARIANT_CHECKS 0
#endif

#if TORRENT_USE_INVARIANT_CHECKS && !TORRENT_USE_ASSERTS
#error "invariant checks cannot be enabled without asserts"
#endif

// for non-exception builds
#ifdef BOOST_NO_EXCEPTIONS
#define TORRENT_TRY if (true)
#define TORRENT_CATCH(x) else if (false)
#define TORRENT_CATCH_ALL else if (false)
#define TORRENT_DECLARE_DUMMY(x, y) x y
#else
#define TORRENT_TRY try
#define TORRENT_CATCH(x) catch(x)
#define TORRENT_CATCH_ALL catch(...)
#define TORRENT_DECLARE_DUMMY(x, y)
#endif // BOOST_NO_EXCEPTIONS

// SSE is x86 / amd64 specific. On top of that, we only
// know how to access it on msvc and gcc (and gcc compatibles).
// GCC requires the user to enable SSE support in order for
// the program to have access to the intrinsics, this is
// indicated by the __SSE4_1__ macro
#ifndef TORRENT_HAS_SSE

#if (defined _M_AMD64 || defined _M_IX86 || defined _M_X64 \
	|| defined __amd64__ || defined __i386 || defined __i386__ \
	|| defined __x86_64__ || defined __x86_64) \
	&& (defined __GNUC__ || (defined _MSC_VER && _MSC_VER >= 1600))
#define TORRENT_HAS_SSE 1
#else
#define TORRENT_HAS_SSE 0
#endif

#endif // TORRENT_HAS_SSE

#if (defined __arm__ || defined __aarch64__ || defined _M_ARM || defined _M_ARM64)
#define TORRENT_HAS_ARM 1
#else
#define TORRENT_HAS_ARM 0
#endif // TORRENT_HAS_ARM

#ifndef __has_builtin
#define __has_builtin(x) 0  // for non-clang compilers
#endif

#if (TORRENT_HAS_SSE && defined __GNUC__)
#	define TORRENT_HAS_BUILTIN_CLZ 1
#elif (TORRENT_HAS_ARM && defined __GNUC__ && !defined __clang__)
#	define TORRENT_HAS_BUILTIN_CLZ 1
#elif (defined __clang__ && __has_builtin(__builtin_clz))
#	define TORRENT_HAS_BUILTIN_CLZ 1
#else
#	define TORRENT_HAS_BUILTIN_CLZ 0
#endif // TORRENT_HAS_BUILTIN_CLZ

#if (TORRENT_HAS_SSE && defined __GNUC__)
#	define TORRENT_HAS_BUILTIN_CTZ 1
#elif (TORRENT_HAS_ARM && defined __GNUC__ && !defined __clang__)
#	define TORRENT_HAS_BUILTIN_CTZ 1
#elif (defined __clang__ && __has_builtin(__builtin_ctz))
#	define TORRENT_HAS_BUILTIN_CTZ 1
#else
#	define TORRENT_HAS_BUILTIN_CTZ 0
#endif // TORRENT_HAS_BUILTIN_CTZ

#if TORRENT_HAS_ARM && defined __ARM_NEON
#	define TORRENT_HAS_ARM_NEON 1
#else
#	define TORRENT_HAS_ARM_NEON 0
#endif // TORRENT_HAS_ARM_NEON

#if TORRENT_HAS_ARM && defined __ARM_FEATURE_CRC32
#	define TORRENT_HAS_ARM_CRC32 1
#else
#if defined TORRENT_FORCE_ARM_CRC32
#	define TORRENT_HAS_ARM_CRC32 1
#else
#	define TORRENT_HAS_ARM_CRC32 0
#endif
#endif // TORRENT_HAS_ARM_CRC32

namespace libtorrent {}

// create alias
namespace lt = libtorrent;

#endif // TORRENT_CONFIG_HPP_INCLUDED
