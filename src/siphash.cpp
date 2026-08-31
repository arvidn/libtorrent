/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/siphash.hpp"
#include "libtorrent/aux_/random.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <cstring>

namespace libtorrent::aux {

namespace {

// reads a word in host-native byte order rather than the spec's
// little-endian; only matters for matching an external implementation,
// which we never do, since digests are only ever compared against our
// own.
std::uint64_t read_word(span<char const> b)
{
	std::uint64_t ret;
	std::memcpy(&ret, b.data(), 8);
	return ret;
}
}

siphash_key random_siphash_key()
{
	siphash_key key;
	crypto_random_bytes({reinterpret_cast<char*>(&key), sizeof(key)});
	return key;
}

std::uint64_t siphash24(siphash_key const& key, span<char const> data)
{
	std::uint64_t v0 = 0x736f6d6570736575ULL ^ key.k0;
	std::uint64_t v1 = 0x646f72616e646f6dULL ^ key.k1;
	std::uint64_t v2 = 0x6c7967656e657261ULL ^ key.k0;
	std::uint64_t v3 = 0x7465646279746573ULL ^ key.k1;

	auto const sipround = [&] {
		v0 += v1;
		v1 = std::rotl(v1, 13);
		v1 ^= v0;
		v0 = std::rotl(v0, 32);
		v2 += v3;
		v3 = std::rotl(v3, 16);
		v3 ^= v2;
		v0 += v3;
		v3 = std::rotl(v3, 21);
		v3 ^= v0;
		v2 += v1;
		v1 = std::rotl(v1, 17);
		v1 ^= v2;
		v2 = std::rotl(v2, 32);
	};

	auto const input_len = static_cast<std::uint64_t>(data.size());

	span<char const> rest = data;
	while (rest.size() >= 8)
	{
		std::uint64_t const m = read_word(rest.first(8));
		rest = rest.subspan(8);
		v3 ^= m;
		sipround();
		sipround();
		v0 ^= m;
	}

	std::array<char, 8> tail{};
	std::copy(rest.begin(), rest.end(), tail.begin());
	tail[7] = static_cast<char>(input_len & 0xff);
	std::uint64_t const b = read_word(tail);

	v3 ^= b;
	sipround();
	sipround();
	v0 ^= b;

	v2 ^= 0xff;
	sipround();
	sipround();
	sipround();
	sipround();

	return v0 ^ v1 ^ v2 ^ v3;
}
}
