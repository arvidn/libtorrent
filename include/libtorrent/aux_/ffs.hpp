/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2017-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FFS_HPP_INCLUDE
#define TORRENT_FFS_HPP_INCLUDE

#include <cstdint>
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {
namespace aux {

	// For a general reference of the problems these routines are about
	// see http://en.wikipedia.org/wiki/Find_first_set

// expects the range to be in big-endian byte order
TORRENT_EXTRA_EXPORT int count_leading_zeros(span<std::uint32_t const> buf);

// expects the range to be in big-endian byte order
TORRENT_EXTRA_EXPORT int count_trailing_ones(span<std::uint32_t const> buf);
}}

#endif // TORRENT_FFS_HPP_INCLUDE
