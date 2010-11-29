/*

Copyright (c) 2005, Arvid Norberg
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

#include <boost/config.hpp>
#include <boost/version.hpp>
#include <stdio.h> // for snprintf

#if defined TORRENT_DEBUG_BUFFERS && !defined TORRENT_DISABLE_POOL_ALLOCATOR
#error TORRENT_DEBUG_BUFFERS only works if you also disable pool allocators
#endif

#ifndef WIN32
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

#ifndef PRId64
#ifdef _WIN32
#define PRId64 "I64d"
#else
#define PRId64 "lld"
#endif
#endif


// ======= GCC =========

#if defined __GNUC__

# if __GNUC__ >= 3
#  define TORRENT_DEPRECATED __attribute__ ((deprecated))
# endif

// GCC pre 4.0 did not have support for the visibility attribute
# if __GNUC__ >= 4
#  if defined(TORRENT_BUILDING_SHARED) || defined(TORRENT_LINKING_SHARED)
#   define TORRENT_EXPORT __attribute__ ((visibility("default")))
#  endif
# endif


// ======= SUNPRO =========

#elif defined __SUNPRO_CC

# if __SUNPRO_CC >= 0x550
#  if defined(TORRENT_BUILDING_SHARED) || defined(TORRENT_LINKING_SHARED)
#   define TORRENT_EXPORT __global
#  endif
# endif

// SunPRO seems to have an overly-strict
// definition of POD types and doesn't
// seem to allow boost::array in unions
#define TORRENT_BROKEN_UNIONS 1

// ======= MSVC =========

#elif defined BOOST_MSVC

#pragma warning(disable: 4258)
#pragma warning(disable: 4251)

# if defined(TORRENT_BUILDING_SHARED)
#  define TORRENT_EXPORT __declspec(dllexport)
# elif defined(TORRENT_LINKING_SHARED)
#  define TORRENT_EXPORT __declspec(dllimport)
# endif

#define TORRENT_DEPRECATED_PREFIX __declspec(deprecated)

#endif


// ======= PLATFORMS =========


// set up defines for target environments
// ==== AMIGA ===
#if defined __AMIGA__ || defined __amigaos__ || defined __AROS__
#define TORRENT_AMIGA
#define TORRENT_USE_MLOCK 0
#define TORRENT_USE_WRITEV 0
#define TORRENT_USE_READV 0
#define TORRENT_USE_IPV6 0
#define TORRENT_USE_BOOST_THREAD 0
#define TORRENT_USE_IOSTREAM 0
// set this to 1 to disable all floating point operations
// (disables some float-dependent APIs)
#define TORRENT_NO_FPU 1
#define TORRENT_USE_I2P 0
#define TORRENT_USE_ICONV 0

// ==== Darwin/BSD ===
#elif (defined __APPLE__ && defined __MACH__) || defined __FreeBSD__ || defined __NetBSD__ \
	|| defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__ \
	|| defined __FreeBSD_kernel__
#define TORRENT_BSD
// we don't need iconv on mac, because
// the locale is always utf-8
#if defined __APPLE__
#define TORRENT_USE_ICONV 0
#endif
#define TORRENT_HAS_FALLOCATE 0

// ==== LINUX ===
#elif defined __linux__
#define TORRENT_LINUX

// ==== MINGW ===
#elif defined __MINGW32__
#define TORRENT_MINGW
#define TORRENT_WINDOWS
#define TORRENT_USE_ICONV 0
#define TORRENT_USE_RLIMIT 0

// ==== WINDOWS ===
#elif defined WIN32
#define TORRENT_WINDOWS
// windows has its own functions to convert
// apple uses utf-8 as its locale, so no conversion
// is necessary
#define TORRENT_USE_ICONV 0
#define TORRENT_USE_RLIMIT 0
#define TORRENT_HAS_FALLOCATE 0

// ==== SOLARIS ===
#elif defined sun || defined __sun 
#define TORRENT_SOLARIS
#define TORRENT_COMPLETE_TYPES_REQUIRED 1

// ==== BEOS ===
#elif defined __BEOS__ || defined __HAIKU__
#define TORRENT_BEOS
#include <storage/StorageDefs.h> // B_PATH_NAME_LENGTH
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_MLOCK 0
#define TORRENT_USE_ICONV 0
#if __GNUCC__ == 2
# if defined(TORRENT_BUILDING_SHARED)
#  define TORRENT_EXPORT __declspec(dllexport)
# elif defined(TORRENT_LINKING_SHARED)
#  define TORRENT_EXPORT __declspec(dllimport)
# endif
#endif
#else
#warning unknown OS, assuming BSD
#define TORRENT_BSD
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

// posix
#elif defined NAME_MAX
#define TORRENT_MAX_PATH NAME_MAX

// none of the above
#else
// this is the maximum number of characters in a
// path element / filename on windows
#define TORRENT_MAX_PATH 255
#warning unknown platform, assuming the longest path is 255

#endif

#if defined TORRENT_WINDOWS && !defined TORRENT_MINGW

// class X needs to have dll-interface to be used by clients of class Y
#pragma warning(disable:4251)
// '_vsnprintf': This function or variable may be unsafe
#pragma warning(disable:4996)

#include <stdarg.h>

inline int snprintf(char* buf, int len, char const* fmt, ...)
{
	va_list lp;
	va_start(lp, fmt);
	int ret = _vsnprintf(buf, len, fmt, lp);
	va_end(lp);
	if (ret < 0) { buf[len-1] = 0; ret = len-1; }
	return ret;
}

#define strtoll _strtoi64
#else
#include <limits.h>
#endif

#if (defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)) \
	&& !defined (TORRENT_UPNP_LOGGING) && TORRENT_USE_IOSTREAM
#define TORRENT_UPNP_LOGGING
#endif

// libiconv presence, not implemented yet
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 1
#endif

#ifndef TORRENT_BROKEN_UNIONS
#define TORRENT_BROKEN_UNIONS 0
#endif

#if defined UNICODE && !defined BOOST_NO_STD_WSTRING
#define TORRENT_USE_WSTRING 1
#else
#define TORRENT_USE_WSTRING 0
#endif // UNICODE

#ifndef TORRENT_HAS_FALLOCATE
#define TORRENT_HAS_FALLOCATE 1
#endif

#ifndef TORRENT_EXPORT
# define TORRENT_EXPORT
#endif

#ifndef TORRENT_DEPRECATED_PREFIX
#define TORRENT_DEPRECATED_PREFIX
#endif

#ifndef TORRENT_DEPRECATED
#define TORRENT_DEPRECATED
#endif

#ifndef TORRENT_COMPLETE_TYPES_REQUIRED
#define TORRENT_COMPLETE_TYPES_REQUIRED 0
#endif

#ifndef TORRENT_USE_RLIMIT
#define TORRENT_USE_RLIMIT 1
#endif

#ifndef TORRENT_USE_IPV6
#define TORRENT_USE_IPV6 1
#endif

#ifndef TORRENT_USE_MLOCK
#define TORRENT_USE_MLOCK 1
#endif

#ifndef TORRENT_USE_WRITEV
#define TORRENT_USE_WRITEV 1
#endif

#ifndef TORRENT_USE_READV
#define TORRENT_USE_READV 1
#endif

#ifndef TORRENT_NO_FPU
#define TORRENT_NO_FPU 0
#endif

#if !defined TORRENT_USE_IOSTREAM && !defined BOOST_NO_IOSTREAM
#define TORRENT_USE_IOSTREAM 1
#else
#define TORRENT_USE_IOSTREAM 0
#endif

#ifndef TORRENT_USE_I2P
#define TORRENT_USE_I2P 1
#endif

#if !defined(TORRENT_READ_HANDLER_MAX_SIZE)
# define TORRENT_READ_HANDLER_MAX_SIZE 256
#endif

#if !defined(TORRENT_WRITE_HANDLER_MAX_SIZE)
# define TORRENT_WRITE_HANDLER_MAX_SIZE 256
#endif

#if defined _MSC_VER && _MSC_VER <= 1200
#define for if (false) {} else for
#endif

#if TORRENT_BROKEN_UNIONS
#define TORRENT_UNION struct
#else
#define TORRENT_UNION union
#endif

// determine what timer implementation we can use
// if one is already defined, don't pick one
// autmatically. This lets the user control this
// from the Jamfile
#if !defined TORRENT_USE_ABSOLUTE_TIME \
	&& !defined TORRENT_USE_QUERY_PERFORMANCE_TIMER \
	&& !defined TORRENT_USE_CLOCK_GETTIME \
	&& !defined TORRENT_USE_BOOST_DATE_TIME \
	&& !defined TORRENT_USE_ECLOCK \
	&& !defined TORRENT_USE_SYSTEM_TIME

#if defined(__MACH__)
#define TORRENT_USE_ABSOLUTE_TIME 1
#elif defined(_WIN32) || defined TORRENT_MINGW
#define TORRENT_USE_QUERY_PERFORMANCE_TIMER 1
#elif defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0
#define TORRENT_USE_CLOCK_GETTIME 1
#elif defined(TORRENT_AMIGA)
#define TORRENT_USE_ECLOCK 1
#elif defined(TORRENT_BEOS)
#define TORRENT_USE_SYSTEM_TIME 1
#else
#define TORRENT_USE_BOOST_DATE_TIME 1
#endif

#endif

#endif // TORRENT_CONFIG_HPP_INCLUDED

