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
#include <limits.h> // for IOV_MAX

#if defined TORRENT_DEBUG_BUFFERS && !defined TORRENT_DISABLE_POOL_ALLOCATOR
#error TORRENT_DEBUG_BUFFERS only works if you also disable pool allocators with TORRENT_DISABLE_POOL_ALLOCATOR
#endif

#if !defined BOOST_ASIO_SEPARATE_COMPILATION && !defined BOOST_ASIO_DYN_LINK
#error you must define either BOOST_ASIO_SEPARATE_COMPILATION or BOOST_ASIO_DYN_LINK in your project in \
	order for asio's declarations to be correct. If you're linking dynamically against libtorrent, define \
	BOOST_ASIO_DYN_LINK otherwise BOOST_ASIO_SEPARATE_COMPILATION. You can also use pkg-config or boost \
	build, to automatically apply these defines
#endif

#if !defined _MSC_VER || _MSC_VER >= 1600
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h> // for INT64_MAX
#else
#if !defined INT64_MAX
#define INT64_MAX 0x7fffffffffffffffLL
#endif
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
#define PRIu32 "u"
#else
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIu32 "u"
#endif
#endif

// backwards compatibility with older versions of boost
#if !defined BOOST_SYMBOL_EXPORT && !defined BOOST_SYMBOL_IMPORT
# if defined _MSC_VER || defined __MINGW32__
#  define BOOST_SYMBOL_EXPORT __declspec(dllexport)
#  define BOOST_SYMBOL_IMPORT __declspec(dllimport)
# elif __GNU__ >= 4
#  define BOOST_SYMBOL_EXPORT __attribute__((visibility("default")))
#  define BOOST_SYMBOL_IMPORT __attribute__((visibility("default")))
# else
#  define BOOST_SYMBOL_EXPORT
#  define BOOST_SYMBOL_IMPORT
# endif
#endif

#if defined TORRENT_BUILDING_SHARED
# define TORRENT_EXPORT BOOST_SYMBOL_EXPORT
#elif defined TORRENT_LINKING_SHARED
# define TORRENT_EXPORT BOOST_SYMBOL_IMPORT
#endif

// when this is specified, export a bunch of extra
// symbols, mostly for the unit tests to reach
#if TORRENT_EXPORT_EXTRA
# if defined TORRENT_BUILDING_SHARED
#  define TORRENT_EXTRA_EXPORT BOOST_SYMBOL_EXPORT
# elif defined TORRENT_LINKING_SHARED
#  define TORRENT_EXTRA_EXPORT BOOST_SYMBOL_IMPORT
# endif
#endif

#ifndef TORRENT_EXTRA_EXPORT
# define TORRENT_EXTRA_EXPORT
#endif

// ======= GCC =========

#if defined __GNUC__

# if __GNUC__ >= 3
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
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#define TORRENT_USE_LOCALE 0
#define TORRENT_CLOSE_MAY_BLOCK 1
#endif
#else
// FreeBSD has a reasonable iconv signature
#define TORRENT_ICONV_ARG (const char**)
#endif
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_SYSCTL 1
#define TORRENT_USE_IFCONF 1


// ==== LINUX ===
#elif defined __linux__
#define TORRENT_LINUX
#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_NETLINK 1
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SALEN 0

// ==== MINGW ===
#elif defined __MINGW32__
#define TORRENT_MINGW
#define TORRENT_WINDOWS
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#define TORRENT_USE_LOCALE 1
#endif
#define TORRENT_USE_RLIMIT 0
#define TORRENT_USE_NETLINK 0
#define TORRENT_USE_GETADAPTERSADDRESSES 1
#define TORRENT_HAS_SALEN 0
#define TORRENT_USE_GETIPFORWARDTABLE 1
#ifndef TORRENT_USE_UNC_PATHS
#define TORRENT_USE_UNC_PATHS 1
#endif

// ==== WINDOWS ===
#elif defined WIN32
#define TORRENT_WINDOWS
#ifndef TORRENT_USE_GETIPFORWARDTABLE
#define TORRENT_USE_GETIPFORWARDTABLE 1
#endif
#define TORRENT_USE_GETADAPTERSADDRESSES 1
#define TORRENT_HAS_SALEN 0
// windows has its own functions to convert
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#define TORRENT_USE_LOCALE 1
#endif
#define TORRENT_USE_RLIMIT 0
#define TORRENT_HAS_FALLOCATE 0
#ifndef TORRENT_USE_UNC_PATHS
#define TORRENT_USE_UNC_PATHS 1
#endif

// ==== SOLARIS ===
#elif defined sun || defined __sun 
#define TORRENT_SOLARIS
#define TORRENT_COMPLETE_TYPES_REQUIRED 1
#define TORRENT_USE_IFCONF 1
#define TORRENT_HAS_SALEN 0

// ==== BEOS ===
#elif defined __BEOS__ || defined __HAIKU__
#define TORRENT_BEOS
#include <storage/StorageDefs.h> // B_PATH_NAME_LENGTH
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_USE_MLOCK 0
#ifndef TORRENT_USE_ICONV
#define TORRENT_USE_ICONV 0
#endif

// ==== GNU/Hurd ===
#elif defined __GNU__
#define TORRENT_HURD
#define TORRENT_USE_IFADDRS 1
#define TORRENT_USE_IFCONF 1

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

#ifndef TORRENT_ICONV_ARG
#define TORRENT_ICONV_ARG (char**)
#endif

// libiconv presence, not implemented yet
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

#ifndef TORRENT_USE_SYSCTL
#define TORRENT_USE_SYSCTL 0
#endif

#ifndef TORRENT_USE_GETIPFORWARDTABLE
#define TORRENT_USE_GETIPFORWARDTABLE 0
#endif

#ifndef TORRENT_USE_LOCALE
#define TORRENT_USE_LOCALE 0
#endif

// set this to true if close() may block on your system
// Mac OS X does this if the file being closed is not fully
// allocated on disk yet for instance. When defined, the disk
// I/O subsytem will use a separate thread for closing files
#ifndef TORRENT_CLOSE_MAY_BLOCK
#define TORRENT_CLOSE_MAY_BLOCK 0
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

#ifndef TORRENT_USE_IOSTREAM
#ifndef BOOST_NO_IOSTREAM
#define TORRENT_USE_IOSTREAM 1
#else
#define TORRENT_USE_IOSTREAM 0
#endif
#endif

// if set to true, piece picker will use less RAM
// but only support up to ~260000 pieces in a torrent
#ifndef TORRENT_COMPACT_PICKER
#define TORRENT_COMPACT_PICKER 0
#endif

#ifndef TORRENT_USE_I2P
#define TORRENT_USE_I2P 1
#endif

#if !defined TORRENT_IOV_MAX
#ifdef IOV_MAX
#define TORRENT_IOV_MAX IOV_MAX
#else
#define TORRENT_IOV_MAX INT_MAX
#endif
#endif

#if !defined(TORRENT_READ_HANDLER_MAX_SIZE)
# define TORRENT_READ_HANDLER_MAX_SIZE 300
#endif

#if !defined(TORRENT_WRITE_HANDLER_MAX_SIZE)
# define TORRENT_WRITE_HANDLER_MAX_SIZE 300
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

#if defined __APPLE__ && defined __MACH__
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

// for non-exception builds
#ifdef BOOST_NO_EXCEPTIONS
#define TORRENT_TRY if (true)
#define TORRENT_CATCH(x) else if (false)
#define TORRENT_DECLARE_DUMMY(x, y) x y
#else
#define TORRENT_TRY try
#define TORRENT_CATCH(x) catch(x)
#define TORRENT_DECLARE_DUMMY(x, y)
#endif // BOOST_NO_EXCEPTIONS


#endif // TORRENT_CONFIG_HPP_INCLUDED

