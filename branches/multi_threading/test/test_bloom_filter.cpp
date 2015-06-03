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
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/sha1_hash.hpp"
#include <boost/cstdint.hpp>

using namespace libtorrent;

void test_set_and_get()
{
	bloom_filter<32> filter;
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
	boost::uint8_t bits[4] = {0x00, 0x00, 0x00, 0x00};

	for (int i = 0; i < 4 * 8; ++i)
	{
		boost::uint8_t t[4] = { boost::uint8_t(i & 0xff), 0, boost::uint8_t(i & 0xff), 0 };
		TEST_CHECK(!has_bits(t, bits, 6));
	}

	for (int i = 0; i < 4 * 8; i += 2)
	{
		boost::uint8_t t[4] = { boost::uint8_t(i & 0xff), 0, boost::uint8_t(i & 0xff), 0 };
		TEST_CHECK(!has_bits(t, bits, 4));
		set_bits(t, bits, 4);
		TEST_CHECK(has_bits(t, bits, 4));
	}

	boost::uint8_t compare[4] = { 0x55, 0x55, 0x55, 0x55};
	TEST_EQUAL(memcmp(compare, bits, 4), 0);
}

void test_count_zeroes()
{
	boost::uint8_t bits[4] = {0x00, 0xff, 0x55, 0xaa};

	TEST_EQUAL(count_zero_bits(bits, 4), 16);

	boost::uint8_t t[4] = { 4, 0, 4, 0 };
	set_bits(t, bits, 4);
	TEST_EQUAL(count_zero_bits(bits, 4), 15);

	boost::uint8_t compare[4] = { 0x10, 0xff, 0x55, 0xaa};
	TEST_EQUAL(memcmp(compare, bits, 4), 0);
}

void test_to_from_string()
{
	boost::uint8_t bits[4] = { 0x10, 0xff, 0x55, 0xaa};

	bloom_filter<4> filter;
	filter.from_string(reinterpret_cast<char*>(bits));

	std::string bits_out = filter.to_string();
	TEST_EQUAL(memcmp(bits_out.c_str(), bits, 4), 0);

	sha1_hash k( "\x01\x00\x02\x00                ");
	TEST_CHECK(!filter.find(k));
	filter.set(k);
	TEST_CHECK(filter.find(k));

	boost::uint8_t compare[4] = { 0x16, 0xff, 0x55, 0xaa};

	bits_out = filter.to_string();
	TEST_EQUAL(memcmp(compare, bits_out.c_str(), 4), 0);
}

TORRENT_TEST(bloom_filter)
{
	test_set_and_get();
	test_set_bits();
	test_count_zeroes();
	test_to_from_string();

	// TODO: test size()
	// TODO: test clear()
}

