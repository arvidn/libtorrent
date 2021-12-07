/*

Copyright (c) 2005, 2008-2009, 2015, 2017-2021, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2017, Steven Siloti
Copyright (c) 2021, Mike Tzou
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/hasher.hpp"
#include "libtorrent/hex.hpp"

#include "test.hpp"

#include <iostream>

using namespace lt;

namespace
{
// test vectors from RFC 3174
// http://www.faqs.org/rfcs/rfc3174.html

struct test_vector_t
{
	string_view input;
	int repetitions;
	string_view hex_output;
};

std::array<test_vector_t, 4> sha1_vectors = {{
	{"abc", 1, "A9993E364706816ABA3E25717850C26C9CD0D89D"},
	{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "84983E441C3BD26EBAAE4AA1F95129E5E54670F1"},
	{"a", 1000000, "34AA973CD4C4DAA4F61EEB2BDBAD27316534016F"},
	{"0123456701234567012345670123456701234567012345670123456701234567", 10, "DEA356A2CDDD90C7A7ECEDC5EBB563934F460452"}
}};

// https://www.dlitz.net/crypto/shad256-test-vectors/
std::array<test_vector_t, 3> sha256_vectors = {{
	{"abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
	{"\xde\x18\x89\x41\xa3\x37\x5d\x3a\x8a\x06\x1e\x67\x57\x6e\x92\x6d", 1, "067c531269735ca7f541fdaca8f0dc76305d3cada140f89372a410fe5eff6e4d"},
	{"a", 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
}};

void test_vector(string_view s, string_view output, int const n = 1)
{
	hasher h;
	for (int i = 0; i < n; i++)
		h.update(s);
	std::string const digest = h.final().to_string();
	std::string const digest_hex = aux::to_hex(digest);

	TEST_EQUAL(digest_hex, output);

	std::string output_hex = digest_hex;
	aux::to_hex(digest, &output_hex[0]);

	TEST_EQUAL(output_hex, digest_hex);
}

template<typename T>
void test_move(string_view const input)
{
	std::string const digest = T(input).final().to_string();

	T tmp1(input);
	T h1(std::move(tmp1));
	TEST_EQUAL(h1.final().to_string(), digest);

	T tmp2(input);
	T h2 = std::move(tmp2);
	TEST_EQUAL(h2.final().to_string(), digest);
}

}

TORRENT_TEST(hasher)
{
	for (auto const& t : sha1_vectors)
	{
		hasher h;
		for (int i = 0; i < t.repetitions; ++i)
			h.update(t.input.data(), int(t.input.size()));

		sha1_hash result;
		aux::from_hex(t.hex_output, result.data());
		TEST_CHECK(result == h.final());
	}
}

TORRENT_TEST(hasher_move)
{
	test_move<hasher>("abc");
}

// http://www.di-mgt.com.au/sha_testvectors.html
TORRENT_TEST(hasher_test_vec1)
{
	test_vector(
		"abc"
		, "a9993e364706816aba3e25717850c26c9cd0d89d"
	);

	test_vector(
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
		, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"
	);

	test_vector(
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhi"
		"jklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
		, "a49b2446a02c645bf419f995b67091253a04a259"
	);

	test_vector(
		"a"
		, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"
		, 1000000
	);

	test_vector(
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
		, "7789f0c9ef7bfc40d93311143dfbe69e2017f592"
		, 16777216
	);
}

TORRENT_TEST(hasher256)
{
	for (auto const& t : sha256_vectors)
	{
		hasher256 h;
		for (int i = 0; i < t.repetitions; ++i)
			h.update(t.input.data(), int(t.input.size()));

		TEST_EQUAL(t.hex_output, aux::to_hex(h.final()));
	}
}

TORRENT_TEST(hasher256_move)
{
	test_move<hasher256>("abc");
}
