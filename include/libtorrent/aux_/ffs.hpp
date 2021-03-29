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

namespace lt::aux {

	// For a general reference of the problems these routines are about
	// see http://en.wikipedia.org/wiki/Find_first_set

	// these functions expect the range to be in big-endian byte order
	TORRENT_EXTRA_EXPORT int count_leading_zeros_sw(span<std::uint32_t const> buf);
	// if this function is called in an unsupported platform, returns -1
	// consider call always count_leading_zeros(buf)
	TORRENT_EXTRA_EXPORT int count_leading_zeros_hw(span<std::uint32_t const> buf);

	// this function statically determines if hardware or software is used
	// and expect the range to be in big-endian byte order
	TORRENT_EXTRA_EXPORT int count_leading_zeros(span<std::uint32_t const> buf);

	// these functions expect the range to be in big-endian byte order
	TORRENT_EXTRA_EXPORT int count_trailing_ones_sw(span<std::uint32_t const> buf);
	// if this function is called in an unsupported platform, returns -1
	// consider call always count_trailing_ones(buf)
	TORRENT_EXTRA_EXPORT int count_trailing_ones_hw(span<std::uint32_t const> buf);

	// this function statically determines if hardware or software is used
	// and expect the range to be in big-endian byte order
	TORRENT_EXTRA_EXPORT int count_trailing_ones(span<std::uint32_t const> buf);

	// returns the index of the most significant set bit.
	TORRENT_EXTRA_EXPORT int log2p1(std::uint32_t v);
}

#endif // TORRENT_FFS_HPP_INCLUDE
