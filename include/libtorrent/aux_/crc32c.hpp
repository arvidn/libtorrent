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
#include <array>
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/export.hpp"

namespace libtorrent::aux {

// this is the crc32c (Castagnoli) polynomial. These three overloads
// cover exactly the fixed-size inputs used for IP-address hashing (DHT
// node ID generation, peer priority): a single 32-bit value (an IPv4
// address, or a pair of ports), a single 64-bit value (an IPv4 address
// pair, or a masked IPv6 address), and a 4-word (32 byte) block (a pair
// of masked IPv6 addresses).
TORRENT_EXTRA_EXPORT std::uint32_t crc32c(std::uint32_t v);
TORRENT_EXTRA_EXPORT std::uint32_t crc32c(std::uint64_t v);
TORRENT_EXTRA_EXPORT std::uint32_t crc32c(std::array<std::uint64_t, 4> const& v);

// the initial state for an incremental crc32c computation, and the
// transform from that running state to the final checksum. Used to
// build up a hash across multiple calls to crc32c_mix()/
// crc32c_mix_lowercase(), e.g. to hash a directory tree where each
// child extends its parent's already-computed state instead of
// re-hashing the shared prefix.
constexpr std::uint32_t crc32c_init = 0xffffffff;
constexpr std::uint32_t crc32c_finish(std::uint32_t const state) { return state ^ 0xffffffff; }

// mixes a single byte into a running crc32c state
TORRENT_EXTRA_EXPORT std::uint32_t crc32c_mix(std::uint32_t state, std::uint8_t byte);

// mixes str into a running crc32c state, lower-casing each character
// first. Used for path element hashing (duplicate filename
// resolution), where names must compare case-insensitively.
TORRENT_EXTRA_EXPORT std::uint32_t crc32c_mix_lowercase(std::uint32_t state, string_view str);
}

#endif
