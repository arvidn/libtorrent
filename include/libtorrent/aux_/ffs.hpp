/*

Copyright (c) 2014-2016, Arvid Norberg, Alden Torres
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

#ifndef TORRENT_FFS_HPP_INCLUDE
#define TORRENT_FFS_HPP_INCLUDE

#include <cstdint>
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {
namespace aux {

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
}}

#endif // TORRENT_FFS_HPP_INCLUDE
