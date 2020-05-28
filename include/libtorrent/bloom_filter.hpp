/*

Copyright (c) 2010-2011, 2015-2017, 2019, Arvid Norberg
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

#ifndef TORRENT_BLOOM_FILTER_HPP_INCLUDED
#define TORRENT_BLOOM_FILTER_HPP_INCLUDED

#include "libtorrent/sha1_hash.hpp"

#include <cmath> // for log()
#include <cstdint>

namespace libtorrent {

	TORRENT_EXTRA_EXPORT void set_bits(std::uint8_t const* k, std::uint8_t* bits, int len);
	TORRENT_EXTRA_EXPORT bool has_bits(std::uint8_t const* k, std::uint8_t const* bits, int len);
	TORRENT_EXTRA_EXPORT int count_zero_bits(std::uint8_t const* bits, int len);

	template <int N>
	struct bloom_filter
	{
		bool find(sha1_hash const& k) const
		{ return has_bits(&k[0], bits, N); }

		void set(sha1_hash const& k)
		{ set_bits(&k[0], bits, N); }

		std::string to_string() const
		{ return std::string(reinterpret_cast<char const*>(&bits[0]), N); }

		void from_string(char const* str)
		{ std::memcpy(bits, str, N); }

		void clear() { std::memset(bits, 0, N); }

		float size() const
		{
			int const c = (std::min)(count_zero_bits(bits, N), (N * 8) - 1);
			int const m = N * 8;
			return std::log(c / float(m)) / (2.f * std::log(1.f - 1.f/m));
		}

		bloom_filter() { clear(); }

	private:
		std::uint8_t bits[N];
	};

}

#endif // TORRENT_BLOOM_FILTER_HPP_INCLUDED
