/*

Copyright (c) 2005-2016, Arvid Norberg
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

#if !defined _MSC_VER || _MSC_VER >= 1600
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS 1
#endif
#endif


#include <boost/config.hpp>
#include <boost/asio/detail/config.hpp>
#include <boost/version.hpp>
#include <boost/detail/endian.hpp>
#include <stdio.h> // for snprintf
#include <limits.h> // for IOV_MAX

#include "libtorrent/export.hpp"

#ifdef __linux__
#include <linux/version.h> // for LINUX_VERSION_CODE and KERNEL_VERSION
#endif // __linux

#if defined TORRENT_DEBUG_BUFFERS && !defined TORRENT_DISABLE_POOL_ALLOCATOR
#error TORRENT_DEBUG_BUFFERS only works if you also disable pool allocators with TORRENT_DISABLE_POOL_ALLOCATOR
#endif

#if !defined BOOST_ASIO_SEPARATE_COMPILATION && !defined BOOST_ASIO_DYN_LINK
#define BOOST_ASIO_SEPARATE_COMPILATION
#endif

#ifndef _MSC_VER
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif
#include <inttypes.h> // for PRId64 et.al.
#endif

#ifndef PRId64
// MinGW uses microsofts runtime
#if defined _MSC_VER || defined __MINGW32__
#define PRId64 "I64d"
#define PRIu64 "I64u"
#define PRIx64 "I64x"
#define PRIu32 "u"
#else
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIu32 "u"
#endif
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

// ======= GCC =========

#if defined __GNUC__

// deprecation markup is only enabled when libtorrent
// headers are included by clients, not while building
// libtorrent itself
# if __GNUC__ >= 3 && !defined TORRENT_BUILDING_LIBRARY
#  define TORRENT_DEPRECATED __attribute__ ((deprecated))
# endif

// ======= SUNPRO =========

#elif defined __SUNPRO_CC

// SunPRO seems to have an overly-strict
// definition of POD types and doesn't
// seem to allow boost::array in unions
#define TORRENT_BROKEN_UNIONS 1

// ======= MSVC =========

#elif defined BOOST_MSVC

#pragma warning(disable: 4258)
#pragma warning(disable: 4251)

// class X needs to have dll-interface to be used by clients of class Y
#pragma warning(disable:4251)
// '_vsnprintf': This function or variable may be unsafe
#pragma warning(disable:4996)

// deprecation markup is only enabled when libtorrent
// headers are included by clients, not while building
// libtorrent itself
#if !defined TORRENT_BUILDING_LIBRARY
# define TORRENT_DEPRECATED __declspec(deprecated)
#endif

#endif


// ======= PLATFORMS =========


// set up defines for target environments
// ==== AMIGA ===
#if defined __AMIGA__ || defined __amigaos__ || defined __AROS__
#define TORRENT_AMIGA
#define TORRENT_USE_IPV6 0
#define TORRENT_USE_BOOST_THREAD 0
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

# define TORRENT_USE_OSATOMIC 1
# ifndef TORRENT_USE_ICONV
#  define TORRENT_USE_ICONV 0
#  define TORRENT_USE_LOCALE 0
# endif
#include <AvailabilityMacros.h>

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
// on OSX, use the built-in common crypto for built-in
# if !defined TORRENT_USE_OPENSSL && !defined TORRENT_USE_GCRYPT
#  define TORRENT_USE_COMMONCRYPTO 1
# endif // TORRENT_USE_OPENSSL
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

// execinfo.h is available in the MacOS X 10.5 SDK.
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
#define TORRENT_USE_EXECINFO 1
#endif

#else // __APPLE__
// FreeBSD has a reasonable iconv signature
// unless we're on glibc
#ifndef __GLIBC__
# define TORRENT_ICONV_ARG (const char**)
#endif
#endif // __APPLE__

#define TORRENT_HAVE_MMAP 1

#define TORRENT_HAS_FALLOCATE 0

#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_SYSCTL 1
#define TORRENT_USE_IFCONF 1


// ==== LINUX ===
#elif defined __linux__
#define TORRENT_LINUX

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
# define TORRENT_USE_PREADV 1
# define TORRENT_USE_PREAD 0
#else
# define TORRENT_USE_PREADV 0
# define TORRENT_USE_PREAD 1
#endif

#define TORRENT_HAVE_MMAP 1
#define TORRENT_USE_NETLINK 1
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SALEN 0

// ===== ANDROID ===== (almost linux, sort of)
#if defined __ANDROID__
#define TORRENT_ANDROID
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_ICONV 0
#define TORRENT_USE_IFADDRS 0
#define TORRENT_USE_MEMALIGN 1
#define TORRENT_USE_FDATASYNC 0
#else // ANDROID
#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_POSIX_MEMALIGN 1
#define TORRENT_USE_FDATASYNC 1

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
#elif defined __MINGW32__
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
#define TORRENT_USE_INTERLOCKED_ATOMIC 1
#ifndef TORRENT_USE_UNC_PATHS
# define TORRENT_USE_UNC_PATHS 1
#endif
// these are emulated on windows
#define TORRENT_USE_PREADV 1
#define TORRENT_USE_PWRITEV 1

// ==== WINDOWS ===
#elif defined WIN32
#define TORRENT_WINDOWS
#ifndef TORRENT_USE_GETIPFORWARDTABLE
# define TORRENT_USE_GETIPFORWARDTABLE 1
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
#define TORRENT_USE_INTERLOCKED_ATOMIC 1
#ifndef TORRENT_USE_UNC_PATHS
# define TORRENT_USE_UNC_PATHS 1
#endif
// these are emulated on windows
#define TORRENT_USE_PREADV 1
#define TORRENT_USE_PWRITEV 1

// ==== WINRT ===
#if defined(WINAPI_FAMILY_PARTITION)
# if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) \
  && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#  define TORRENT_WINRT
#  define TORRENT_USE_CRYPTOGRAPHIC_BUFFER 1
# endif
#endif

// ==== SOLARIS ===
#elif defined sun || defined __sun
#define TORRENT_SOLARIS
#define TORRENT_COMPLETE_TYPES_REQUIRED 1
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SALEN 0
#define TORRENT_HAS_SEM_RELTIMEDWAIT 1
#define TORRENT_HAVE_MMAP 1
#define TORRENT_USE_SOLARIS_ATOMIC 1

// ==== BEOS ===
#elif defined __BEOS__ || defined __HAIKU__
#define TORRENT_BEOS
#include <storage/StorageDefs.h> // B_PATH_NAME_LENGTH
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_BEOS_ATOMIC 1
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#endif

// ==== GNU/Hurd ===
#elif defined __GNU__
#define TORRENT_HURD
#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_IFCONF 1

// ==== eCS(OS/2) ===
#elif defined __OS2__
#define TORRENT_OS2
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_IFCONF 1
#define TORRENT_USE_SYSCTL 1
#define TORRENT_USE_IPV6 0
#define TORRENT_ICONV_ARG (const char**)
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

#if defined __GNUC__ && !(defined TORRENT_USE_OSATOMIC \
	|| defined TORRENT_USE_INTERLOCKED_ATOMIC \
	|| defined TORRENT_USE_BEOS_ATOMIC \
	|| defined TORRENT_USE_SOLARIS_ATOMIC)
// atomic operations in GCC were introduced in 4.1.1
# if (__GNUC__ >= 4 && __GNUC_MINOR__ >= 1 && __GNUC_PATCHLEVEL__ >= 1) || __GNUC__ > 4
#  define TORRENT_USE_GCC_ATOMIC 1
# endif
#endif

// on windows, NAME_MAX refers to Unicode characters
// on linux it refers to bytes (utf-8 encoded)
// TODO: Make this count Unicode characters instead of bytes on windows

// windows
#if defined FILENAME_MAX
#define TORRENT_MAX_PATH FILENAME_MAX

// beos
#elif defined B_PATH_NAME_LENGTH
#define TORRENT_MAX_PATH B_PATH_NAME_LENGTH

// solaris
#elif defined MAXPATH
#define TORRENT_MAX_PATH MAXPATH

// none of the above
#else
// this is the maximum number of characters in a
// path element / filename on windows and also on many filesystems commonly used
// on linux
#define TORRENT_MAX_PATH 255

#ifdef _MSC_VER
#pragma message ( "unknown platform, assuming the longest path is 255" )
#else
#warning "unknown platform, assuming the longest path is 255"
#endif

#endif

#define TORRENT_UNUSED(x) (void)(x)

#if (defined _MSC_VER && _MSC_VER < 1900) && !defined TORRENT_MINGW

#include <stdarg.h>

// internal
#ifdef __cplusplus
inline
#else
static
#endif
int snprintf(char* buf, int len, char const* fmt, ...)
{
	va_list lp;
	int ret;
	va_start(lp, fmt);
	ret = _vsnprintf(buf, len, fmt, lp);
	va_end(lp);
	if (ret < 0) { buf[len-1] = 0; ret = len-1; }
	return ret;
}

#define strtoll _strtoi64
#endif

// at the highest warning level, clang actually warns about functions
// that could be marked noreturn.
#if defined __clang__ || defined __GNUC__
#define TORRENT_NO_RETURN __attribute((noreturn))
#else
#define TORRENT_NO_RETURN
#endif

#ifdef _GLIBCXX_USE_NOEXCEPT
#define TORRENT_EXCEPTION_THROW_SPECIFIER _GLIBCXX_USE_NOEXCEPT
#else
#if __cplusplus <= 199711L || defined BOOST_NO_CXX11_NOEXCEPT
#define TORRENT_EXCEPTION_THROW_SPECIFIER throw()
#else
#define TORRENT_EXCEPTION_THROW_SPECIFIER noexcept
#endif
#endif // __GLIBC__

#if __cplusplus <= 199711L || defined BOOST_NO_CXX11_FINAL
#define TORRENT_FINAL
#else
#define TORRENT_FINAL final
#endif

#if __cplusplus <= 199711L || defined BOOST_NO_CXX11_FINAL
#define TORRENT_OVERRIDE
#else
#define TORRENT_OVERRIDE override
#endif

#ifndef TORRENT_ICONV_ARG
#define TORRENT_ICONV_ARG (char**)
#endif

#if defined __GNUC__ || defined __clang__
#define TORRENT_FORMAT(fmt, ellipsis) __attribute__((__format__(__printf__, fmt, ellipsis)))
#else
#define TORRENT_FORMAT(fmt, ellipsis)
#endif

#ifndef TORRENT_USE_INTERLOCKED_ATOMIC
#define TORRENT_USE_INTERLOCKED_ATOMIC 0
#endif

#ifndef TORRENT_USE_GCC_ATOMIC
#define TORRENT_USE_GCC_ATOMIC 0
#endif

#ifndef TORRENT_USE_OSATOMIC
#define TORRENT_USE_OSATOMIC 0
#endif

#ifndef TORRENT_USE_BEOS_ATOMIC
#define TORRENT_USE_BEOS_ATOMIC 0
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

#ifndef TORRENT_HAS_SEM_RELTIMEDWAIT
#define TORRENT_HAS_SEM_RELTIMEDWAIT 0
#endif

#ifndef TORRENT_USE_MEMALIGN
#define TORRENT_USE_MEMALIGN 0
#endif

#ifndef TORRENT_USE_POSIX_MEMALIGN
#define TORRENT_USE_POSIX_MEMALIGN 0
#endif

#ifndef TORRENT_USE_LOCALE
#define TORRENT_USE_LOCALE 0
#endif

#ifndef TORRENT_BROKEN_UNIONS
#define TORRENT_BROKEN_UNIONS 0
#endif

#ifndef TORRENT_USE_WSTRING
#if !defined BOOST_NO_STD_WSTRING
#define TORRENT_USE_WSTRING 1
#else
#define TORRENT_USE_WSTRING 0
#endif // BOOST_NO_STD_WSTRING
#endif // TORRENT_USE_WSTRING

#ifndef TORRENT_HAS_FALLOCATE
#define TORRENT_HAS_FALLOCATE 1
#endif

#ifndef TORRENT_DEPRECATED
#define TORRENT_DEPRECATED
#endif

#ifndef TORRENT_USE_COMMONCRYPTO
#define TORRENT_USE_COMMONCRYPTO 0
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

#ifndef TORRENT_USE_IPV6
#define TORRENT_USE_IPV6 1
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

// whether function-local static variables are thread safe. In c++11 and later
// they are (except msvc)
#ifndef TORRENT_THREADSAFE_STATIC
#if __cplusplus < 199711L || (defined _MSC_VER && _MSC_VER <= 1800)
#define TORRENT_THREADSAFE_STATIC 0
#else
#define TORRENT_THREADSAFE_STATIC 1
#endif
#endif

#ifndef TORRENT_USE_I2P
#define TORRENT_USE_I2P 1
#endif

#ifndef TORRENT_HAS_BOOST_UNORDERED
#define TORRENT_HAS_BOOST_UNORDERED 1
#endif

#if !defined TORRENT_IOV_MAX
#ifdef IOV_MAX
#define TORRENT_IOV_MAX IOV_MAX
#else
#define TORRENT_IOV_MAX INT_MAX
#endif
#endif

#if !defined(TORRENT_READ_HANDLER_MAX_SIZE)
# ifdef _GLIBCXX_DEBUG
#  define TORRENT_READ_HANDLER_MAX_SIZE 400
# else
// if this is not divisible by 8, we're wasting space
#  define TORRENT_READ_HANDLER_MAX_SIZE 336
# endif
#endif

#if !defined(TORRENT_WRITE_HANDLER_MAX_SIZE)
# ifdef _GLIBCXX_DEBUG
#  define TORRENT_WRITE_HANDLER_MAX_SIZE 400
# else
// if this is not divisible by 8, we're wasting space
#  define TORRENT_WRITE_HANDLER_MAX_SIZE 336
# endif
#endif

#if defined _MSC_VER && _MSC_VER <= 1200
// this is here to provide a standard-conforming for
// keyword for old versions of msvc. The pragmas are
// there to silence the warning it produces by using
// a constant as conditional
#define for \
	__pragma( warning(push) ) \
	__pragma( warning(disable:4127) ) \
	if (false) {} else \
	__pragma( warning(pop) )
	for
#endif

#if TORRENT_BROKEN_UNIONS
#define TORRENT_UNION struct
#else
#define TORRENT_UNION union
#endif

#if defined __GNUC__
#define TORRENT_FUNCTION __PRETTY_FUNCTION__
#else
#define TORRENT_FUNCTION __FUNCTION__
#endif


// debug builds have asserts enabled by default, release
// builds have asserts if they are explicitly enabled by
// the release_asserts macro.
#ifndef TORRENT_USE_ASSERTS
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
#define TORRENT_USE_ASSERTS 1
#else
#define TORRENT_USE_ASSERTS 0
#endif
#endif // TORRENT_USE_ASSERTS

#if defined TORRENT_DEBUG && TORRENT_USE_ASSERTS \
	&& !defined TORRENT_DISABLE_INVARIANT_CHECKS
#define TORRENT_USE_INVARIANT_CHECKS 1
#else
#define TORRENT_USE_INVARIANT_CHECKS 0
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
	&& (defined __GNUC__ || defined _MSC_VER)
#define TORRENT_HAS_SSE 1
#else
#define TORRENT_HAS_SSE 0
#endif

#endif // TORRENT_HAS_SSE


#endif // TORRENT_CONFIG_HPP_INCLUDED

