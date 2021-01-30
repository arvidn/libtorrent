/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
