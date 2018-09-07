/*

Copyright (c) 2015, Arvid Norberg
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

#include "test.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/hex.hpp" // from_hex

using namespace lt;

static sha1_hash to_hash(char const* s)
{
	sha1_hash ret;
	aux::from_hex({s, 40}, ret.data());
	return ret;
}

TORRENT_TEST(sha1_hash)
{
	sha1_hash h1(nullptr);
	sha1_hash h2(nullptr);
	TEST_CHECK(h1 == h2);
	TEST_CHECK(!(h1 != h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(h1.is_all_zeros());

	h1 = to_hash("0123456789012345678901234567890123456789");
	h2 = to_hash("0113456789012345678901234567890123456789");

	TEST_CHECK(h2 < h1);
	TEST_CHECK(h2 == h2);
	TEST_CHECK(h1 == h1);
	h2.clear();
	TEST_CHECK(h2.is_all_zeros());

	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 &= h2;
	TEST_CHECK(h1 == to_hash("fffff000000000000000fffff000000000000000"));

	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 |= h2;
	TEST_CHECK(h1 == to_hash("fffffffffffffff00000fffffffffffffff00000"));

	h2 = to_hash("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
	h1 ^= h2;
	TEST_CHECK(h1 == to_hash("f0f0f0f0f0f0f0ff0f0ff0f0f0f0f0f0f0ff0f0f"));
	TEST_CHECK(h1 != h2);

	h2 = sha1_hash("                    ");
	TEST_CHECK(h2 == to_hash("2020202020202020202020202020202020202020"));

	h1 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 <<= 12;
	TEST_CHECK(h1 == to_hash("fffffff0000000000ffffffffff0000000000000"));
	h1 >>= 12;
	TEST_CHECK(h1 == to_hash("000fffffff0000000000ffffffffff0000000000"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 <<= 1;
	TEST_CHECK(h1 == to_hash("e000000000000000000000000000000000000000"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 <<= 1;
	TEST_CHECK(h1 == to_hash("000000000000000000000000000000000000000e"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 >>= 1;
	TEST_CHECK(h1 == to_hash("0000000000000000000000000000000000000003"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 >>= 1;
	TEST_CHECK(h1 == to_hash("3800000000000000000000000000000000000000"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 >>= 32;
	TEST_CHECK(h1 == to_hash("0000000070000000000000000000000000000000"));
	h1 >>= 33;
	TEST_CHECK(h1 == to_hash("0000000000000000380000000000000000000000"));
	h1 <<= 33;
	TEST_CHECK(h1 == to_hash("0000000070000000000000000000000000000000"));
}

TORRENT_TEST(count_leading_zeroes)
{
	std::vector<std::pair<char const*, int>> const tests = {
		{ "ffffffffffffffffffffffffffffffffffffffff", 0 },
		{ "0000000000000000000000000000000000000000", 160 },
		{ "fff0000000000000000000000000000000000000", 0 },
		{ "7ff0000000000000000000000000000000000000", 1 },
		{ "3ff0000000000000000000000000000000000000", 2 },
		{ "1ff0000000000000000000000000000000000000", 3 },
		{ "0ff0000000000000000000000000000000000000", 4 },
		{ "07f0000000000000000000000000000000000000", 5 },
		{ "03f0000000000000000000000000000000000000", 6 },
		{ "01f0000000000000000000000000000000000000", 7 },
		{ "00f0000000000000000000000000000000000000", 8 },
		{ "0070000000000000000000000000000000000000", 9 },
		{ "0030000000000000000000000000000000000000", 10 },
		{ "0010000000000000000000000000000000000000", 11 },
		{ "0000000ffff00000000000000000000000000000", 28 },
		{ "00000007fff00000000000000000000000000000", 29 },
		{ "00000003fff00000000000000000000000000000", 30 },
		{ "00000001fff00000000000000000000000000000", 31 },
		{ "00000000fff00000000000000000000000000000", 32 },
		{ "000000007ff00000000000000000000000000000", 33 },
		{ "000000003ff00000000000000000000000000000", 34 },
		{ "000000001ff00000000000000000000000000000", 35 },
	};

	for (auto const& t : tests)
	{
		std::printf("%s\n", t.first);
		TEST_EQUAL(to_hash(t.first).count_leading_zeroes(), t.second);
	}
}
