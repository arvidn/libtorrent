/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SIPHASH_HPP_INCLUDED
#define TORRENT_SIPHASH_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"

#include <cstdint>

namespace libtorrent::aux {

// secret key for siphash24(). without it, an attacker cannot craft input
// colliding with a chosen digest.
struct siphash_key
{
	std::uint64_t k0 = 0;
	std::uint64_t k1 = 0;
};

static_assert(sizeof(siphash_key) == 16, "siphash_key must not have padding");

// generates a siphash_key from a strong entropy source
TORRENT_EXTRA_EXPORT siphash_key random_siphash_key();

// SipHash-2-4. unlike an unkeyed hash, a party that knows the algorithm
// but not the key cannot construct colliding input.
TORRENT_EXTRA_EXPORT std::uint64_t siphash24(siphash_key const& key, span<char const> data);
}

#endif
