/*

Copyright (c) 2011-2013, 2016-2021, Arvid Norberg
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

	TORRENT_EXTRA_EXPORT std::uint32_t random(std::uint32_t m);

namespace aux {

	TORRENT_EXTRA_EXPORT std::mt19937& random_engine();

	template <typename Range>
	void random_shuffle(Range& range)
	{
#ifdef TORRENT_BUILD_SIMULATOR
		// in simulations, we want all shuffles to be deterministic (as long as
		// the random engine is deterministic
		if (range.size() == 0) return;
		for (auto i = range.size() - 1; i > 0; --i) {
			auto const other = random(std::uint32_t(i));
			if (i == other) continue;
			using std::swap;
			swap(range.data()[i], range.data()[other]);
		}
#else
		std::shuffle(range.data(), range.data() + range.size(), random_engine());
#endif
	}

	// Fills the buffer with pseudo random bytes.
	//
	// This functions perform differently under different setups
	// For Windows and all platforms when compiled with libcrypto, it
	// generates cryptographically random bytes.
	// If the above conditions are not true, then a standard
	// fill of bytes is used.
	TORRENT_EXTRA_EXPORT void random_bytes(span<char> buffer);

	// Fills the buffer with random bytes from a strong entropy source. This can
	// be used to generate secrets.
	TORRENT_EXTRA_EXPORT void crypto_random_bytes(span<char> buffer);
}
}

#endif // TORRENT_RANDOM_HPP_INCLUDED
