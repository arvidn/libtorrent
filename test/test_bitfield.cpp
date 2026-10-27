/*

Copyright (c) 2013-2020, 2022, Arvid Norberg
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2017, Falcosc
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/cpuid.hpp"
#include "libtorrent/aux_/ceil_div.hpp"
#include <cstdint>
#include <cstdlib>
#include <limits>

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
	TEST_EQUAL(test1.num_words(), 1);
	TEST_EQUAL(test1.num_bytes(), 2);
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
	TEST_EQUAL(test1.num_words(), 1);
	TEST_EQUAL(test1.num_bytes(), 3);
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

TORRENT_TEST(test_iterator_arithmetic)
{
	bitfield test1(128);
	test1.set_bit(0);
	test1.set_bit(1);
	test1.set_bit(40);
	test1.set_bit(127);
	TEST_EQUAL(std::count(test1.begin(), test1.end(), true), 4);
	TEST_EQUAL(std::count(test1.begin() + 1, test1.end(), true), 3);
	TEST_EQUAL(std::count(test1.begin() + 2, test1.end(), true), 2);
	TEST_EQUAL(std::count(test1.begin() + 2, test1.begin() + 41, true), 1);
	TEST_EQUAL(std::count(test1.begin() + 41, test1.end(), true), 1);
	TEST_EQUAL(std::count(test1.begin() + 41, test1.begin() + 126, true), 0);
	TEST_EQUAL(std::count((test1.begin() + 30) + 10, test1.begin() + 50, true), 1);
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

#if !TORRENT_HAS_ARM
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
	TEST_EQUAL(test1.num_bytes(), 0);
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
	TEST_EQUAL(test1.num_words(), (123 + 31) / 32);
	TEST_EQUAL(test1.num_bytes(), (123 + 7) / 8);
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
	TEST_EQUAL(test2.num_words(), 1);
	TEST_EQUAL(test2.num_bytes(), 1);
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

TORRENT_TEST(ceil_div_zero)
{
	TEST_EQUAL(aux::ceil_div(0, 5), 0);
	TEST_EQUAL(aux::ceil_div(0, 1), 0);
}

TORRENT_TEST(ceil_div_exact_multiple)
{
	TEST_EQUAL(aux::ceil_div(10, 5), 2);
	TEST_EQUAL(aux::ceil_div(32, 32), 1);
	TEST_EQUAL(aux::ceil_div(64, 32), 2);
}

TORRENT_TEST(ceil_div_remainder)
{
	TEST_EQUAL(aux::ceil_div(1, 5), 1);
	TEST_EQUAL(aux::ceil_div(6, 5), 2);
	TEST_EQUAL(aux::ceil_div(11, 5), 3);
	TEST_EQUAL(aux::ceil_div(33, 32), 2);
	TEST_EQUAL(aux::ceil_div(63, 32), 2);
}

TORRENT_TEST(ceil_div_one_below_and_at_boundary)
{
	// the two values flanking a divisor boundary must round differently
	TEST_EQUAL(aux::ceil_div(31, 32), 1);
	TEST_EQUAL(aux::ceil_div(32, 32), 1);
	TEST_EQUAL(aux::ceil_div(33, 32), 2);
}

TORRENT_TEST(ceil_div_divisor_one)
{
	TEST_EQUAL(aux::ceil_div(0, 1), 0);
	TEST_EQUAL(aux::ceil_div(1, 1), 1);
	TEST_EQUAL(aux::ceil_div(12345, 1), 12345);
}

TORRENT_TEST(ceil_div_non_power_of_two_divisor)
{
	// exercises the plain division path, not the shift-optimized one
	TEST_EQUAL(aux::ceil_div(0, 7), 0);
	TEST_EQUAL(aux::ceil_div(1, 7), 1);
	TEST_EQUAL(aux::ceil_div(7, 7), 1);
	TEST_EQUAL(aux::ceil_div(8, 7), 2);
	TEST_EQUAL(aux::ceil_div(21, 7), 3);
	TEST_EQUAL(aux::ceil_div(22, 7), 4);
}

TORRENT_TEST(ceil_div_large_values_no_overflow)
{
	// this is the case (bits + divisor - 1) / divisor gets wrong (UB, signed
	// overflow) when bits is close to INT_MAX
	int const max = std::numeric_limits<int>::max();
	TEST_EQUAL(aux::ceil_div(max, 32), (max / 32) + 1);
	TEST_EQUAL(aux::ceil_div(max, 1), max);
	TEST_EQUAL(aux::ceil_div(max - 1, max), 1);
	TEST_EQUAL(aux::ceil_div(max, max), 1);
}

TORRENT_TEST(ceil_div_other_integral_types)
{
	TEST_EQUAL(aux::ceil_div(std::int64_t(10), std::int64_t(4)), 3);
	TEST_EQUAL(aux::ceil_div(std::uint32_t(10), std::uint32_t(4)), 3u);
	TEST_EQUAL(aux::ceil_div(std::size_t(10), std::size_t(4)), std::size_t(3));

	std::int64_t const max64 = std::numeric_limits<std::int64_t>::max();
	TEST_EQUAL(aux::ceil_div(max64, std::int64_t(32)), (max64 / 32) + 1);
}
