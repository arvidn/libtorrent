/*

Copyright (c) 2010-2011, 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BLOOM_FILTER_HPP_INCLUDED
#define TORRENT_BLOOM_FILTER_HPP_INCLUDED

#include "libtorrent/sha1_hash.hpp"

#include <cmath> // for log()
#include <cstdint>

namespace lt::aux {

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
