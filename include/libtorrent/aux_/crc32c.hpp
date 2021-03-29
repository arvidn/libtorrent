/*

Copyright (c) 2010, 2014, 2016-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CRC32C_HPP_INCLUDE
#define TORRENT_CRC32C_HPP_INCLUDE

#include <cstdint>
#include "libtorrent/aux_/export.hpp"

namespace lt::aux {

	// this is the crc32c (Castagnoli) polynomial
	TORRENT_EXTRA_EXPORT std::uint32_t crc32c_32(std::uint32_t);
	TORRENT_EXTRA_EXPORT std::uint32_t crc32c(std::uint64_t const*, int);
}

#endif
