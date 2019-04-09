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
#include "libtorrent/aux_/cpuid.hpp"
#include <cstdlib>

using namespace lt;

namespace {

void print_bitfield(bitfield const& b)
{
	std::string out;
	out.reserve(std::size_t(b.size()));
	for (bool bit : b)
		out += bit ? '1' : '0';
	std::printf("%s\n", out.c_str());
}

void test_iterators(bitfield& test1)
{
	test1.set_all();
	int num = 0;

	std::printf("expecting %d ones\n", test1.size());
	for (bitfield::const_iterator i = test1.begin(); i != test1.end(); ++i)
	{
		std::printf("%d", *i);
		TEST_EQUAL(*i, true);
		num += *i;
	}
	std::printf("\n");
	TEST_EQUAL(num, test1.size());
	TEST_EQUAL(num, test1.count());
}

} // anonymous namespace

TORRENT_TEST(bitfield)
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
	int distance = int(std::distance(test1.begin(), test1.end()));
	std::printf("distance: %d\n", distance);
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
	TEST_EQUAL(test1.count(), 100);
	test1.resize(200, false);
	TEST_CHECK(test1.all_set() == false);
	TEST_EQUAL(test1.count(), 100);
	test1.resize(50, false);
	TEST_CHECK(test1.all_set() == true);
	TEST_EQUAL(test1.count(), 50);
	test1.resize(101, true);
	TEST_CHECK(test1.all_set() == true);
	TEST_EQUAL(test1.count(), 101);

	std::uint8_t b1[] = { 0x08, 0x10 };
	test1.assign(reinterpret_cast<char*>(b1), 14);
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
}

TORRENT_TEST(test_assign3)
{
	bitfield test1;
	std::uint8_t b2[] = { 0x08, 0x10, 0xff, 0xff, 0xff, 0xff, 0xf, 0xc, 0x7f };
	test1.assign(reinterpret_cast<char*>(b2), 72);
	print_bitfield(test1);
	TEST_EQUAL(test1.count(), 47);

	std::uint8_t b3[] = { 0x08, 0x10, 0xff, 0xff, 0xff, 0xff, 0xf, 0xc };
	test1.assign(reinterpret_cast<char*>(b3), 64);
	print_bitfield(test1);
	TEST_EQUAL(test1.count(), 40);
}

TORRENT_TEST(test_iterators)
{
	bitfield test1;
	for (int i = 0; i < 100; ++i)
	{
		test1.resize(i, false);
		test_iterators(test1);
	}
}

TORRENT_TEST(test_assign)
{
	std::array<char, 16> b;
	bitfield test1;

	for (std::size_t i = 0; i < 4; ++i)
	{
		b[i] = char(0xc0);
		test1.assign(&b[i], 2);
		print_bitfield(test1);
		TEST_EQUAL(test1.count(), 2);
		TEST_EQUAL(test1.all_set(), true);
	}
}

TORRENT_TEST(test_assign2)
{
	std::array<char, 16> b;
	bitfield test1;
	for (std::size_t i = 0; i < 4; ++i)
	{
		std::memset(&b[i], 0xff, 5);
		b[i + 5] = char(0xc0);
		test1.assign(&b[i], 32 + 8 + 2);
		print_bitfield(test1);
		TEST_EQUAL(test1.count(), 32 + 8 + 2);
		TEST_EQUAL(test1.all_set(), true);
	}

#if TORRENT_HAS_ARM
	TORRENT_ASSERT(aux::arm_neon_support);
#else
	TORRENT_ASSERT(!aux::arm_neon_support);
#endif
}

TORRENT_TEST(test_resize_val)
{
	std::array<char, 8> b;
	b.fill(-52);

	bitfield test1(b.data(), 8 * 8);
	print_bitfield(test1);
	TEST_EQUAL(test1.size(), 8 * 8);
	TEST_EQUAL(test1.count(), 4 * 8);

	for (int i = 1; i < 4 * 8; ++i)
	{
		test1.resize(8 * 8 + i, true);
		print_bitfield(test1);
		TEST_EQUAL(test1.count(), 4 * 8 + i);
	}
}

TORRENT_TEST(test_resize_up)
{
	std::array<char, 8> b;
	b.fill(-52);

	bitfield test1(b.data(), 8 * 8);
	print_bitfield(test1);
	TEST_EQUAL(test1.size(), 8 * 8);
	TEST_EQUAL(test1.count(), 4 * 8);

	for (int i = 1; i < 5 * 8; ++i)
	{
		test1.resize(8 * 8 + i);
		print_bitfield(test1);
		TEST_EQUAL(test1.size(), 8 * 8 + i);
		TEST_EQUAL(test1.count(), 4 * 8);
	}
}

TORRENT_TEST(test_resize_down)
{
	std::array<char, 8> b;
	b.fill(0x55);

	bitfield test1(b.data(), 8 * 8);

	for (int i = 8 * 8; i > -1; --i)
	{
		test1.resize(i);
		print_bitfield(test1);
		TEST_EQUAL(test1.size(), i);
		TEST_EQUAL(test1.count(), i / 2);
	}
}

TORRENT_TEST(find_first_set_empty)
{
	bitfield test1(0);
	TEST_EQUAL(test1.find_first_set(), -1);
}

TORRENT_TEST(find_first_set_small)
{
	bitfield test1(10, false);
	TEST_EQUAL(test1.find_first_set(), -1);
}

TORRENT_TEST(find_first_set_large)
{
	bitfield test1(100, false);
	TEST_EQUAL(test1.find_first_set(), -1);
}

TORRENT_TEST(find_first_set_early)
{
	bitfield test1(100, false);
	test1.set_bit(4);
	TEST_EQUAL(test1.find_first_set(), 4);
}

TORRENT_TEST(find_first_set_late)
{
	bitfield test1(100, false);
	test1.set_bit(98);
	TEST_EQUAL(test1.find_first_set(), 98);
}

TORRENT_TEST(find_last_clear_empty)
{
	bitfield test1(0);
	TEST_EQUAL(test1.find_last_clear(), -1);
}

TORRENT_TEST(find_last_clear_small)
{
	bitfield test1(10, true);
	TEST_EQUAL(test1.find_last_clear(), -1);
}

TORRENT_TEST(find_last_clear_large)
{
	bitfield test1(100, true);
	TEST_EQUAL(test1.find_last_clear(), -1);
}

TORRENT_TEST(find_last_clear_early)
{
	bitfield test1(100, true);
	test1.clear_bit(4);
	TEST_EQUAL(test1.find_last_clear(), 4);
}

TORRENT_TEST(find_last_clear_late)
{
	bitfield test1(100, true);
	test1.clear_bit(98);
	TEST_EQUAL(test1.find_last_clear(), 98);
}

TORRENT_TEST(find_last_clear_misc)
{
	bitfield test1(100, true);
	test1.clear_bit(11);
	test1.clear_bit(91);
	TEST_EQUAL(test1.find_last_clear(), 91);

	bitfield test2(78, true);
	test2.clear_bit(12);
	test2.clear_bit(43);
	test2.clear_bit(34);
	TEST_EQUAL(test2.find_last_clear(), 43);

	bitfield test3(123, true);
	test3.clear_bit(49);
	test3.clear_bit(33);
	test3.clear_bit(32);
	test3.clear_bit(50);
	TEST_EQUAL(test3.find_last_clear(), 50);

	bitfield test4(1000, true);
	test4.clear_bit(11);
	test4.clear_bit(91);
	test4.clear_bit(14);
	test4.clear_bit(15);
	test4.clear_bit(89);
	TEST_EQUAL(test4.find_last_clear(), 91);
}

TORRENT_TEST(not_initialized)
{
	// check a not initialized empty bitfield
	bitfield test1(0);
	TEST_EQUAL(test1.none_set(), true);
	TEST_EQUAL(test1.all_set(), false);
	TEST_EQUAL(test1.size(), 0);
	TEST_EQUAL(test1.num_words(), 0);
	TEST_EQUAL(test1.empty(), true);
	TEST_CHECK(test1.data() == nullptr);
	TEST_EQUAL(test1.count(), 0);
	TEST_EQUAL(test1.find_first_set(), -1);
	TEST_EQUAL(test1.find_last_clear(), -1);

	test1.clear_all();
	TEST_EQUAL(test1.size(), 0);

	test1.clear();
	TEST_EQUAL(test1.size(), 0);

	test1.set_all();
	TEST_EQUAL(test1.size(), 0);

	// don't test methods which aren't defined for empty sets:
	// get_bit, clear_bit, set_bit
}

TORRENT_TEST(self_assign)
{
	bitfield test1(123, false);
	bitfield* self_ptr = &test1;
	test1 = *self_ptr;
	TEST_EQUAL(test1.size(), 123);
	TEST_EQUAL(test1.count(), 0);
}

TORRENT_TEST(not_initialized_assign)
{
	// check a not initialized empty bitfield
	bitfield test1(0);
	std::uint8_t b1[] = { 0xff };
	test1.assign(reinterpret_cast<char*>(b1), 8);
	TEST_EQUAL(test1.count(), 8);
}

TORRENT_TEST(not_initialized_resize)
{
	// check a not initialized empty bitfield
	bitfield test1(0);
	test1.resize(8, true);
	TEST_EQUAL(test1.count(), 8);

	bitfield test2(0);
	test2.resize(8);
	TEST_EQUAL(test2.size(), 8);
}

TORRENT_TEST(bitfield_index_range)
{
	typed_bitfield<int> b1(16);
	int sum = 0;
	for (auto i : b1.range())
	{
		sum += i;
	}
	TEST_EQUAL(sum, 15 * 16 / 2);
}

