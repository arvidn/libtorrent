/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2019, Arvid Norberg
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
#include "libtorrent/info_hash.hpp"
#include "libtorrent/sha1_hash.hpp"

#include <unordered_set>

using namespace lt;

namespace {
	sha1_hash const none1;
	sha1_hash const zeroes1("00000000000000000000");
	sha1_hash const ones1("11111111111111111111");
	sha1_hash const twos1("22222222222222222222");

	sha256_hash const none2;
	sha256_hash const zeroes2("00000000000000000000000000000000");
	sha256_hash const ones2("11111111111111111111111111111111");
	sha256_hash const twos2("22222222222222222222222222222222");
}

TORRENT_TEST(ordering)
{
	// make sure the comparison function establishes a total order

	std::vector<lt::info_hash_t> examples{
		{none1, none2},
		{none1, zeroes2},
		{none1, ones2},
		{none1, twos2},

		{zeroes1, none2},
		{zeroes1, zeroes2},
		{zeroes1, ones2},
		{zeroes1, twos2},

		{ones1, none2},
		{ones1, zeroes2},
		{ones1, ones2},
		{ones1, twos2},

		{twos1, none2},
		{twos1, zeroes2},
		{twos1, ones2},
		{twos1, twos2},
	};

	for (auto const& a : examples)
	{
		for (auto const& b : examples)
		{
			if (a < b) TEST_CHECK(a != b);
			if (b < a) TEST_CHECK(a != b);
			if (a != b) TEST_CHECK(b != a);
			TEST_CHECK((a == b) == (b == a));
			TEST_CHECK((a == b) == !(b != a));
			TEST_CHECK((a == b) == !(a != b));
			if (a < b) TEST_CHECK(!(b < a));
			TEST_CHECK((!(a < b) && !(b < a)) == (a == b));
			for (auto const& c : examples)
			{
				if (a < b && b < c) TEST_CHECK(a < c);
			}
		}
	}
}

TORRENT_TEST(has)
{
	{
		lt::info_hash_t a{none1, none2};
		TEST_EQUAL(a.has_v1(), false);
		TEST_EQUAL(a.has_v2(), false);
		TEST_EQUAL(a.has(protocol_version::V1), false);
		TEST_EQUAL(a.has(protocol_version::V2), false);
		TEST_EQUAL(a.get_best(), none1);
	}

	{
		lt::info_hash_t a{ones1, none2};
		TEST_EQUAL(a.has_v1(), true);
		TEST_EQUAL(a.has_v2(), false);
		TEST_EQUAL(a.has(protocol_version::V1), true);
		TEST_EQUAL(a.has(protocol_version::V2), false);
		TEST_EQUAL(a.get_best(), ones1);
	}

	{
		lt::info_hash_t a{ones1, twos2};
		TEST_EQUAL(a.has_v1(), true);
		TEST_EQUAL(a.has_v2(), true);
		TEST_EQUAL(a.has(protocol_version::V1), true);
		TEST_EQUAL(a.has(protocol_version::V2), true);
		TEST_EQUAL(a.get_best(), twos1);
	}

	{
		lt::info_hash_t a{none1, ones2};
		TEST_EQUAL(a.has_v1(), false);
		TEST_EQUAL(a.has_v2(), true);
		TEST_EQUAL(a.has(protocol_version::V1), false);
		TEST_EQUAL(a.has(protocol_version::V2), true);
		TEST_EQUAL(a.get_best(), ones1);
	}
}

TORRENT_TEST(std_hash)
{
	std::unordered_set<lt::info_hash_t> test;

	test.emplace(none1, none2);
	test.emplace(none1, zeroes2);
	test.emplace(none1, ones2);
	test.emplace(none1, twos2);

	test.emplace(zeroes1, none2);
	test.emplace(zeroes1, zeroes2);
	test.emplace(zeroes1, ones2);
	test.emplace(zeroes1, twos2);

	test.emplace(ones1, none2);
	test.emplace(ones1, zeroes2);
	test.emplace(ones1, ones2);
	test.emplace(ones1, twos2);

	test.emplace(twos1, none2);
	test.emplace(twos1, zeroes2);
	test.emplace(twos1, ones2);
	test.emplace(twos1, twos2);

	TEST_EQUAL(test.size(), 16);
}
