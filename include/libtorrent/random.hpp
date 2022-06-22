/*

Copyright (c) 2011-2013, 2016-2021, Arvid Norberg
Copyright (c) 2016, Alden Torres
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
