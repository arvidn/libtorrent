/*

Copyright (c) 2017-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DOWNLOAD_PRIORITY_HPP_INCLUDED
#define TORRENT_DOWNLOAD_PRIORITY_HPP_INCLUDED

#include "libtorrent/units.hpp"

namespace lt {

using download_priority_t = aux::strong_typedef<std::uint8_t, struct download_priority_tag>;

// Don't download the file or piece. Partial pieces may still be downloaded when
// setting file priorities.
constexpr download_priority_t dont_download{0};

// The default priority for files and pieces.
constexpr download_priority_t default_priority{4};

// The lowest priority for files and pieces.
constexpr download_priority_t low_priority{1};

// The highest priority for files and pieces.
constexpr download_priority_t top_priority{7};

}

#endif
