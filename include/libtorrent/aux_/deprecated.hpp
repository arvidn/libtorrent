/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DEPRECATED_HPP_INCLUDED
#define TORRENT_DEPRECATED_HPP_INCLUDED

#if !defined TORRENT_BUILDING_LIBRARY
# define TORRENT_DEPRECATED [[deprecated]]
#else
# define TORRENT_DEPRECATED
#endif

#if defined __clang__

// ====== CLANG ========

# if !defined TORRENT_BUILDING_LIBRARY
// TODO: figure out which version of clang this is supported in
#  define TORRENT_DEPRECATED_ENUM __attribute__ ((deprecated))
# endif

#elif defined __GNUC__

// ======== GCC ========

// deprecation markup is only enabled when libtorrent
// headers are included by clients, not while building
// libtorrent itself
# if __GNUC__ >= 6 && !defined TORRENT_BUILDING_LIBRARY
#  define TORRENT_DEPRECATED_ENUM __attribute__ ((deprecated))
# endif

#endif

#ifndef TORRENT_DEPRECATED_ENUM
#define TORRENT_DEPRECATED_ENUM
#endif

#endif
