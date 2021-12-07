/*

Copyright (c) 2015-2017, 2020-2021, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/bloom_filter.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/sha1_hash.hpp"
#include <cstdint>

using namespace lt;

namespace {

void test_set_and_get()
{
	aux::bloom_filter<32> filter;
	sha1_hash k1 = hasher("test1", 5).final();
	sha1_hash k2 = hasher("test2", 5).final();
	sha1_hash k3 = hasher("test3", 5).final();
	sha1_hash k4 = hasher("test4", 5).final();
	TEST_CHECK(!filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k1);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k4);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(filter.find(k4));
}

void test_set_bits()
{
	std::uint8_t bits[4] = {0x00, 0x00, 0x00, 0x00};

	for (int i = 0; i < 4 * 8; ++i)
	{
		std::uint8_t t[4] = { std::uint8_t(i & 0xff), 0, std::uint8_t(i & 0xff), 0 };
		TEST_CHECK(!aux::has_bits(t, bits, 6));
	}

	for (int i = 0; i < 4 * 8; i += 2)
	{
		std::uint8_t t[4] = { std::uint8_t(i & 0xff), 0, std::uint8_t(i & 0xff), 0 };
		TEST_CHECK(!aux::has_bits(t, bits, 4));
		aux::set_bits(t, bits, 4);
		TEST_CHECK(aux::has_bits(t, bits, 4));
	}

	std::uint8_t compare[4] = { 0x55, 0x55, 0x55, 0x55};
	TEST_EQUAL(memcmp(compare, bits, 4), 0);
}

void test_count_zeroes()
{
	std::uint8_t bits[4] = {0x00, 0xff, 0x55, 0xaa};

	TEST_EQUAL(aux::count_zero_bits(bits, 4), 16);

	std::uint8_t t[4] = { 4, 0, 4, 0 };
	aux::set_bits(t, bits, 4);
	TEST_EQUAL(aux::count_zero_bits(bits, 4), 15);

	std::uint8_t compare[4] = { 0x10, 0xff, 0x55, 0xaa};
	TEST_EQUAL(std::memcmp(compare, bits, 4), 0);
}

void test_to_from_string()
{
	std::uint8_t bits[4] = { 0x10, 0xff, 0x55, 0xaa};

	aux::bloom_filter<4> filter;
	filter.from_string(reinterpret_cast<char*>(bits));

	std::string bits_out = filter.to_string();
	TEST_EQUAL(memcmp(bits_out.c_str(), bits, 4), 0);

	sha1_hash k("\x01\x00\x02\x00                ");
	TEST_CHECK(!filter.find(k));
	filter.set(k);
	TEST_CHECK(filter.find(k));

	std::uint8_t compare[4] = { 0x16, 0xff, 0x55, 0xaa};

	bits_out = filter.to_string();
	TEST_EQUAL(memcmp(compare, bits_out.c_str(), 4), 0);
}

} // anonymous namespace

TORRENT_TEST(bloom_filter)
{
	test_set_and_get();
	test_set_bits();
	test_count_zeroes();
	test_to_from_string();

	// TODO: test size()
	// TODO: test clear()
}
