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

#ifndef WIN32
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

#ifndef PRId64
// MinGW uses microsofts runtime
#if defined _MSC_VER || defined __MINGW32__
#define PRId64 "I64d"
#else
#define PRId64 "lld"
#endif
#endif


// ======= GCC =========

#if defined __GNUC__

# define TORRENT_DEPRECATED __attribute__ ((deprecated))

// GCC pre 4.0 did not have support for the visibility attribute
# if __GNUC__ >= 4
#  if defined(TORRENT_BUILDING_SHARED) || defined(TORRENT_LINKING_SHARED)
#   define TORRENT_EXPORT __attribute__ ((visibility("default")))
#  else
#   define TORRENT_EXPORT
#  endif
# else
#  define TORRENT_EXPORT
# endif


// ======= SUNPRO =========

#elif defined __SUNPRO_CC

# if __SUNPRO_CC >= 0x550
#  if defined(TORRENT_BUILDING_SHARED) || defined(TORRENT_LINKING_SHARED)
#   define TORRENT_EXPORT __global
#  else
#   define TORRENT_EXPORT
#  endif
# else
#  define TORRENT_EXPORT
# endif


// ======= MSVC =========

#elif defined BOOST_MSVC

#pragma warning(disable: 4258)
#pragma warning(disable: 4251)

# if defined(TORRENT_BUILDING_SHARED)
#  define TORRENT_EXPORT __declspec(dllexport)
# elif defined(TORRENT_LINKING_SHARED)
#  define TORRENT_EXPORT __declspec(dllimport)
# else
#  define TORRENT_EXPORT
# endif

#define TORRENT_DEPRECATED_PREFIX __declspec(deprecated)



// ======= GENERIC COMPILER =========

#else
# define TORRENT_EXPORT
#endif



#ifndef TORRENT_DEPRECATED_PREFIX
#define TORRENT_DEPRECATED_PREFIX
#endif

#ifndef TORRENT_DEPRECATED
#define TORRENT_DEPRECATED
#endif

// set up defines for target environments
#if (defined __APPLE__ && __MACH__) || defined __FreeBSD__ || defined __NetBSD__ \
	|| defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__ \
	|| defined __FreeBSD_kernel__
#define TORRENT_BSD
#define TORRENT_HAS_FALLOCATE 0
#elif defined __linux__
#define TORRENT_LINUX
#elif defined __MINGW32__
#define TORRENT_MINGW
#define TORRENT_WINDOWS
#define TORRENT_HAS_FALLOCATE 0
#define TORRENT_ICONV_ARG (const char**)
#elif defined WIN32
#define TORRENT_WINDOWS
#define TORRENT_HAS_FALLOCATE 0
#elif defined sun || defined __sun 
#define TORRENT_SOLARIS
#define TORRENT_COMPLETE_TYPES_REQUIRED 1
#else
#warning unknown OS, assuming BSD
#define TORRENT_BSD
#endif

#define TORRENT_USE_IPV6 1
#define TORRENT_USE_MLOCK 1
#define TORRENT_USE_READV 1
#define TORRENT_USE_WRITEV 1
#define TORRENT_USE_IOSTREAM 1

#if defined TORRENT_BSD || defined TORRENT_LINUX || defined TORRENT_SOLARIS
#define TORRENT_USE_RLIMIT 1
#else
#define TORRENT_USE_RLIMIT 0
#endif

#ifndef TORRENT_HAS_FALLOCATE
#define TORRENT_HAS_FALLOCATE 1
#endif

// should wpath or path be used?
#if defined UNICODE && !defined BOOST_FILESYSTEM_NARROW_ONLY \
	&& BOOST_VERSION >= 103400 && !defined __APPLE__ && !defined TORRENT_MINGW
#define TORRENT_USE_WPATH 1
#else
#define TORRENT_USE_WPATH 0
#endif

// set this to 1 to disable all floating point operations
// (disables some float-dependent APIs)
#define TORRENT_NO_FPU 0

// make sure NAME_MAX is defined
#ifndef NAME_MAX
#ifdef MAXPATH
#define NAME_MAX MAXPATH
#else
// this is the maximum number of characters in a
// path element / filename on windows
#define NAME_MAX 255
#endif // MAXPATH
#endif // NAME_MAX

#if defined TORRENT_WINDOWS && !defined TORRENT_MINGW

#pragma warning(disable:4251) // class X needs to have dll-interface to be used by clients of class Y

#include <stdarg.h>
// '_vsnprintf': This function or variable may be unsafe
#pragma warning(disable:4996)

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

#if (defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)) && !defined (TORRENT_UPNP_LOGGING)
#define TORRENT_UPNP_LOGGING
#endif

#if !TORRENT_USE_WPATH && (defined TORRENT_LINUX || defined TORRENT_MINGW)
// libiconv presence, not implemented yet
#define TORRENT_USE_LOCALE_FILENAMES 1
#else
#define TORRENT_USE_LOCALE_FILENAMES 0
#endif

#if !defined(TORRENT_READ_HANDLER_MAX_SIZE)
# define TORRENT_READ_HANDLER_MAX_SIZE 256
#endif

#if !defined(TORRENT_WRITE_HANDLER_MAX_SIZE)
# define TORRENT_WRITE_HANDLER_MAX_SIZE 256
#endif

#ifndef TORRENT_ICONV_ARG
#define TORRENT_ICONV_ARG (char**)
#endif

#if defined _MSC_VER && _MSC_VER <= 1200
#define for if (false) {} else for
#endif

// determine what timer implementation we can use

#if defined(__MACH__)
#define TORRENT_USE_ABSOLUTE_TIME 1
#elif defined(_WIN32)
#define TORRENT_USE_QUERY_PERFORMANCE_TIMER 1
#elif defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0
#define TORRENT_USE_CLOCK_GETTIME 1
#else
#define TORRENT_USE_BOOST_DATE_TIME 1
#endif

#endif // TORRENT_CONFIG_HPP_INCLUDED

