/*

Copyright (c) 2004, 2006, 2010, 2012, 2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, Jan Berkel
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

#ifndef TORRENT_VERSION_HPP_INCLUDED
#define TORRENT_VERSION_HPP_INCLUDED

#include "libtorrent/aux_/export.hpp"
#include <cstdint>

#define LIBTORRENT_VERSION_MAJOR 2
#define LIBTORRENT_VERSION_MINOR 0
#define LIBTORRENT_VERSION_TINY 2

// the format of this version is: MMmmtt
// M = Major version, m = minor version, t = tiny version
#define LIBTORRENT_VERSION_NUM ((LIBTORRENT_VERSION_MAJOR * 10000) + (LIBTORRENT_VERSION_MINOR * 100) + LIBTORRENT_VERSION_TINY)

#define LIBTORRENT_VERSION "2.0.2.0"
#define LIBTORRENT_REVISION "bee27fc69"

namespace libtorrent {

	// the major, minor and tiny versions of libtorrent
	constexpr int version_major = 2;
	constexpr int version_minor = 0;
	constexpr int version_tiny = 2;

	// the libtorrent version in string form
	constexpr char const* version_str = "2.0.2.0";

	// the git commit of this libtorrent version
	constexpr std::uint64_t version_revision = 0xbee27fc69;

	// returns the libtorrent version as string form in this format:
	// "<major>.<minor>.<tiny>.<tag>"
	TORRENT_EXPORT char const* version();

}

namespace lt = libtorrent;

#endif
