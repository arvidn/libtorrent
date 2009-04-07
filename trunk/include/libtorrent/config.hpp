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

#if defined(__GNUC__) && __GNUC__ >= 4

#define TORRENT_DEPRECATED __attribute__ ((deprecated))

# if defined(TORRENT_BUILDING_SHARED) || defined(TORRENT_LINKING_SHARED)
#  define TORRENT_EXPORT __attribute__ ((visibility("default")))
# else
#  define TORRENT_EXPORT
# endif

#elif defined(__GNUC__)

# define TORRENT_EXPORT

#elif defined(BOOST_MSVC)

# if defined(TORRENT_BUILDING_SHARED)
#  define TORRENT_EXPORT __declspec(dllexport)
# elif defined(TORRENT_LINKING_SHARED)
#  define TORRENT_EXPORT __declspec(dllimport)
# else
#  define TORRENT_EXPORT
# endif

#else
# define TORRENT_EXPORT
#endif

#ifndef TORRENT_DEPRECATED
#define TORRENT_DEPRECATED
#endif

// set up defines for target environments
#if (defined __APPLE__ && __MACH__) || defined __FreeBSD__ || defined __NetBSD__ \
	|| defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__ \
	|| defined __FreeBSD_kernel__
#define TORRENT_BSD
#elif defined __linux__
#define TORRENT_LINUX
#elif defined WIN32
#define TORRENT_WINDOWS
#elif defined sun || defined __sun 
#define TORRENT_SOLARIS
#else
#warning unkown OS, assuming BSD
#define TORRENT_BSD
#endif

#define TORRENT_USE_IPV6 1
#define TORRENT_USE_MLOCK 1
#define TORRENT_USE_READV 1
#define TORRENT_USE_WRITEV 1
#define TORRENT_USE_IOSTREAM 1

// should wpath or path be used?
#if defined UNICODE && !defined BOOST_FILESYSTEM_NARROW_ONLY \
	&& BOOST_VERSION >= 103400 && !defined __APPLE__
#define TORRENT_USE_WPATH 1
#else
#define TORRENT_USE_WPATH 0
#endif

#ifdef TORRENT_WINDOWS
// this is the maximum number of characters in a
// path element / filename on windows
#define NAME_MAX 255
#define snprintf _snprintf
#define strtoll _strtoi64
#endif

#if (defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)) && !defined (TORRENT_UPNP_LOGGING)
#define TORRENT_UPNP_LOGGING
#endif

#if !TORRENT_USE_WPATH && defined TORRENT_LINUX
// libiconv presnce, not implemented yet
#define TORRENT_USE_LOCALE_FILENAMES 1
#else
#define TORRENT_USE_LOCALE_FILENAMES 0
#endif


#endif // TORRENT_CONFIG_HPP_INCLUDED

