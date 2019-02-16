/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/hasher.hpp"
#include "libtorrent/hex.hpp"

#include "test.hpp"

using namespace lt;

namespace
{
// test vectors from RFC 3174
// http://www.faqs.org/rfcs/rfc3174.html

struct test_case
{
	string_view input;
	int repeat_count;
	string_view result;
};

std::array<test_case, 4> const test_array =
{{
	{"abc"_sv, 1, "A9993E364706816ABA3E25717850C26C9CD0D89D"_sv},
	{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"_sv, 1, "84983E441C3BD26EBAAE4AA1F95129E5E54670F1"_sv},
	{"a"_sv, 1000000, "34AA973CD4C4DAA4F61EEB2BDBAD27316534016F"_sv},
	{"0123456701234567012345670123456701234567012345670123456701234567"_sv, 10, "DEA356A2CDDD90C7A7ECEDC5EBB563934F460452"_sv}
}};

void test_vector(std::string s, std::string output, int const n = 1)
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

}

TORRENT_TEST(hasher)
{
	for (auto const& test : test_array)
	{
		hasher h;
		for (int i = 0; i < test.repeat_count; ++i)
			h.update(test.input);

		sha1_hash result;
		aux::from_hex(test.result, result.data());
		TEST_CHECK(result == h.final());
	}
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
