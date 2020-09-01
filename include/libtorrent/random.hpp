/*

Copyright (c) 2011-2013, 2016-2019, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RANDOM_HPP_INCLUDED
#define TORRENT_RANDOM_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"

#include <cstdint>
#include <random>
#include <algorithm>

namespace libtorrent {
namespace aux {

	TORRENT_EXTRA_EXPORT std::mt19937& random_engine();

	template <typename Range>
	void random_shuffle(Range& range)
	{
		std::shuffle(range.data(), range.data() + range.size(), random_engine());
	}

	// Fills the buffer with random bytes.
	//
	// This functions perform differently under different setups
	// For Windows and all platforms when compiled with libcrypto, it
	// generates cryptographically random bytes.
	// If the above conditions are not true, then a standard
	// fill of bytes is used.
	TORRENT_EXTRA_EXPORT void random_bytes(span<char> buffer);
}

	TORRENT_EXTRA_EXPORT std::uint32_t random(std::uint32_t m);
}

#endif // TORRENT_RANDOM_HPP_INCLUDED
