/*

Copyright (c) 2004, 2006, 2010, 2012, 2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, Jan Berkel
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_VERSION_HPP_INCLUDED
#define TORRENT_VERSION_HPP_INCLUDED

#include "libtorrent/aux_/export.hpp"
#include <cstdint>

#define LIBTORRENT_VERSION_MAJOR 2
#define LIBTORRENT_VERSION_MINOR 0
#define LIBTORRENT_VERSION_TINY 0

// the format of this version is: MMmmtt
// M = Major version, m = minor version, t = tiny version
#define LIBTORRENT_VERSION_NUM ((LIBTORRENT_VERSION_MAJOR * 10000) + (LIBTORRENT_VERSION_MINOR * 100) + LIBTORRENT_VERSION_TINY)

#define LIBTORRENT_VERSION "2.0.0.0"
#define LIBTORRENT_REVISION "ab46e04ac"

namespace libtorrent {

	// the major, minor and tiny versions of libtorrent
	constexpr int version_major = 2;
	constexpr int version_minor = 0;
	constexpr int version_tiny = 0;

	// the libtorrent version in string form
	constexpr char const* version_str = "2.0.0.0";

	// the git commit of this libtorrent version
	constexpr std::uint64_t version_revision = 0xab46e04ac;

	// returns the libtorrent version as string form in this format:
	// "<major>.<minor>.<tiny>.<tag>"
	TORRENT_EXPORT char const* version();

}

namespace lt = libtorrent;

#endif
