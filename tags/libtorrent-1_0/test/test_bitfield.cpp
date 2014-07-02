/*

Copyright (c) 2008-2013, Arvid Norberg
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
#include "libtorrent/bitfield.hpp"

using namespace libtorrent;

void print_bitfield(bitfield const& b)
{
	std::string out;
	for (int i = 0; i < b.size(); ++i)
	{
		out += b.get_bit(i) ? "1" : "0";
	}
	printf("%s\n", out.c_str());
}

void test_iterators(bitfield& test1)
{
	test1.set_all();
	int num = 0;

	printf("expecting %d ones\n", int(test1.size()));
	for (bitfield::const_iterator i = test1.begin(); i != test1.end(); ++i)
	{
		printf("%d", *i);
		TEST_EQUAL(*i, true);
		num += *i;
	}
	printf("\n");
	TEST_EQUAL(num, test1.size());
	TEST_EQUAL(num, test1.count());
}

int test_main()
{
	bitfield test1(10, false);
	TEST_EQUAL(test1.size(), 10);
	TEST_EQUAL(test1.empty(), false);
	TEST_EQUAL(test1.count(), 0);
	test1.set_bit(9);
	TEST_EQUAL(test1.count(), 1);
	test1.clear_bit(9);
	TEST_EQUAL(test1.count(), 0);
	test1.set_bit(2);
	TEST_EQUAL(test1.count(), 1);
	test1.set_bit(1);
	test1.set_bit(9);
	TEST_EQUAL(test1.count(), 3);
	TEST_CHECK(test1.all_set() == false);
	test1.clear_bit(2);
	TEST_EQUAL(test1.count(), 2);
	int distance = std::distance(test1.begin(), test1.end());
	fprintf(stderr, "%d\n", distance);
	TEST_CHECK(distance == 10);
	
	print_bitfield(test1);

	test1.set_all();
	TEST_EQUAL(test1.count(), 10);

	test1.clear_all();
	TEST_EQUAL(test1.count(), 0);

	test1.resize(2);
	test1.set_bit(0);
	test1.resize(16, true);
	TEST_EQUAL(test1.count(), 15);
	test1.resize(20, true);
	TEST_EQUAL(test1.count(), 19);
	TEST_EQUAL(test1.get_bit(0), true);
	TEST_EQUAL(test1.get_bit(1), false);

	bitfield test2 = test1;
	print_bitfield(test2);
	TEST_EQUAL(test2.count(), 19);
	TEST_EQUAL(test2.get_bit(0), true);
	TEST_EQUAL(test2.get_bit(1), false);
	TEST_EQUAL(test2.get_bit(2), true);

	test1.set_bit(1);
	test1.resize(1);
	TEST_EQUAL(test1.count(), 1);

	test1.resize(100, true);
	TEST_CHECK(test1.all_set() == true);
	TEST_CHECK(test1.count() == 100);
	test1.resize(200, false);
	TEST_CHECK(test1.all_set() == false);
	TEST_CHECK(test1.count() == 100);
	test1.resize(50, false);
	TEST_CHECK(test1.all_set() == true);
	TEST_CHECK(test1.count() == 50);
	test1.resize(101, true);
	TEST_CHECK(test1.all_set() == true);
	TEST_CHECK(test1.count() == 101);

	boost::uint8_t b1[] = { 0x08, 0x10 };
	test1.assign((char*)b1, 14);
	print_bitfield(test1);
	TEST_EQUAL(test1.count(), 2);
	TEST_EQUAL(test1.get_bit(3), false);
	TEST_EQUAL(test1.get_bit(4), true);
	TEST_EQUAL(test1.get_bit(5), false);
	TEST_EQUAL(test1.get_bit(10), false);
	TEST_EQUAL(test1.get_bit(11), true);
	TEST_EQUAL(test1.get_bit(12), false);

	test1 = bitfield();
	TEST_EQUAL(test1.size(), 0);
	TEST_EQUAL(test1.empty(), true);
	TEST_EQUAL(bitfield().empty(), true);

	test1 = test2;
	TEST_EQUAL(test1.size(), 20);
	TEST_EQUAL(test1.count(), 19);
	TEST_EQUAL(test1.get_bit(0), true);
	TEST_EQUAL(test1.get_bit(1), false);
	TEST_EQUAL(test1.get_bit(2), true);

	boost::uint8_t b2[] = { 0x08, 0x10, 0xff, 0xff, 0xff, 0xff, 0xf, 0xc, 0x7f };
	test1.assign((char*)b2, 72);
	print_bitfield(test1);
	TEST_EQUAL(test1.count(), 47);

	boost::uint8_t b3[] = { 0x08, 0x10, 0xff, 0xff, 0xff, 0xff, 0xf, 0xc };
	test1.assign((char*)b3, 64);
	print_bitfield(test1);
	TEST_EQUAL(test1.count(), 40);

	for (int i = 0; i < 100; ++i)
	{
		test1.resize(i, false);
		test_iterators(test1);
	}
	return 0;
}
