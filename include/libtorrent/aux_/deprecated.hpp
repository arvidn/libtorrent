/*

Copyright (c) 2019, Arvid Norberg
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

#ifndef TORRENT_DEPRECATED_HPP_INCLUDED
#define TORRENT_DEPRECATED_HPP_INCLUDED

#if defined __clang__

// ====== CLANG ========

# if !defined TORRENT_BUILDING_LIBRARY
// TODO: figure out which version of clang this is supported in
#  define TORRENT_DEPRECATED __attribute__ ((deprecated))
#  define TORRENT_DEPRECATED_ENUM __attribute__ ((deprecated))
#  define TORRENT_DEPRECATED_MEMBER __attribute__ ((deprecated))
# endif

#elif defined __GNUC__

// ======== GCC ========

// deprecation markup is only enabled when libtorrent
// headers are included by clients, not while building
// libtorrent itself
# if __GNUC__ >= 3 && !defined TORRENT_BUILDING_LIBRARY
#  define TORRENT_DEPRECATED __attribute__ ((deprecated))
# endif

# if __GNUC__ >= 6 && !defined TORRENT_BUILDING_LIBRARY
#  define TORRENT_DEPRECATED_ENUM __attribute__ ((deprecated))
#  define TORRENT_DEPRECATED_MEMBER __attribute__ ((deprecated))
# endif

#elif defined _MSC_VER

// ======= MSVC =========

// deprecation markup is only enabled when libtorrent
// headers are included by clients, not while building
// libtorrent itself
#if !defined TORRENT_BUILDING_LIBRARY
# define TORRENT_DEPRECATED __declspec(deprecated)
#endif

#endif

#ifndef TORRENT_DEPRECATED
#define TORRENT_DEPRECATED
#endif

#ifndef TORRENT_DEPRECATED_ENUM
#define TORRENT_DEPRECATED_ENUM
#endif

#ifndef TORRENT_DEPRECATED_MEMBER
#define TORRENT_DEPRECATED_MEMBER
#endif

#endif
