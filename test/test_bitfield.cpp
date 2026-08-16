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
#include <cstdlib>
#include <vector>

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

	std::uint8_t b1[] = {0x08, 0x10};
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
	std::uint8_t b2[] = {0x08, 0x10, 0xff, 0xff, 0xff, 0xff, 0xf, 0xc, 0x7f};
	test1.assign(reinterpret_cast<char*>(b2), 72);
	print_bitfield(test1);
	TEST_EQUAL(test1.count(), 47);

	std::uint8_t b3[] = {0x08, 0x10, 0xff, 0xff, 0xff, 0xff, 0xf, 0xc};
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
	std::uint8_t b1[] = {0xff};
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

TORRENT_TEST(visit_set_network_order)
{
	// The third word is empty and the final byte has seven padding bits set.
	std::array<std::uint8_t, 13> const bytes = {{
		0x80,
		0x01,
		0x00,
		0x01,
		0x80,
		0x00,
		0x00,
		0x01,
		0x00,
		0x00,
		0x00,
		0x00,
		0xff,
	}};
	typed_bitfield<int> const bits(reinterpret_cast<char const*>(bytes.data()), 97);

	std::vector<int> visited;
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return true;
	});

	std::vector<int> const expected = {0, 15, 31, 32, 63, 96};
	TEST_CHECK(visited == expected);
}

TORRENT_TEST(visit_set_early_exit)
{
	typed_bitfield<int> bits(96, false);
	for (int const index : {0, 31, 32, 63, 64, 95})
		bits.set_bit(index);

	std::vector<int> visited;
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return index != 32;
	});

	std::vector<int> const expected = {0, 31, 32};
	TEST_CHECK(visited == expected);
}

TORRENT_TEST(visit_set_all_clear_word)
{
	// the middle word is entirely clear, the others have a few bits set
	typed_bitfield<int> bits(96, false);
	bits.set_bit(0);
	bits.set_bit(31);
	bits.set_bit(64);
	bits.set_bit(95);

	std::vector<int> visited;
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return true;
	});

	std::vector<int> const expected = {0, 31, 64, 95};
	TEST_CHECK(visited == expected);
}

TORRENT_TEST(visit_set_all_set_word)
{
	// the middle word is entirely set, the others have a single bit set
	typed_bitfield<int> bits(96, false);
	bits.set_bit(0);
	for (int i = 32; i < 64; ++i)
		bits.set_bit(i);
	bits.set_bit(95);

	std::vector<int> visited;
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return true;
	});

	std::vector<int> expected = {0};
	for (int i = 32; i < 64; ++i)
		expected.push_back(i);
	expected.push_back(95);
	TEST_CHECK(visited == expected);
}

TORRENT_TEST(visit_set_all_set_word_early_exit)
{
	// a single, fully set word that is also the last word (size is an exact
	// multiple of 32, so there are no trailing padding bits to worry about)
	typed_bitfield<int> const bits(32, true);

	std::vector<int> visited;
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return index != 10;
	});

	std::vector<int> const expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	TEST_CHECK(visited == expected);
}

TORRENT_TEST(visit_set_dense_popcount_branch)
{
	// Test the popcount > 7 branch (where bits are tested sequentially)
	// and ensure it respects early exit as well as non-32-aligned sizes.
	typed_bitfield<int> bits(60, false);
	// Set 12 bits in the first word (word 0, popcount > 7)
	for (int i = 0; i < 12; ++i)
		bits.set_bit(i);
	// Set 10 bits in the second word (word 1, popcount > 7)
	for (int i = 32; i < 42; ++i)
		bits.set_bit(i);

	std::vector<int> visited;
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return true;
	});

	TEST_EQUAL(visited.size(), 22);
	for (int i = 0; i < 12; ++i)
		TEST_EQUAL(visited[static_cast<std::size_t>(i)], i);
	for (int i = 0; i < 10; ++i)
		TEST_EQUAL(visited[static_cast<std::size_t>(12 + i)], 32 + i);

	// Early exit inside dense branch
	visited.clear();
	bits.visit_set([&visited](int const index) {
		visited.push_back(index);
		return index != 5;
	});
	TEST_EQUAL(visited.size(), 6);
	for (int i = 0; i <= 5; ++i)
		TEST_EQUAL(visited[static_cast<std::size_t>(i)], i);
}

TORRENT_TEST(move_semantics)
{
	// Move construction and assignment on bitfield
	bitfield b1(100, true);
	b1.clear_bit(50);
	TEST_EQUAL(b1.size(), 100);
	TEST_EQUAL(b1.count(), 99);

	bitfield b2(std::move(b1));
	TEST_EQUAL(b2.size(), 100);
	TEST_EQUAL(b2.count(), 99);
	TEST_EQUAL(b2.get_bit(50), false);
	TEST_EQUAL(b2.get_bit(49), true);

	bitfield b3;
	b3 = std::move(b2);
	TEST_EQUAL(b3.size(), 100);
	TEST_EQUAL(b3.count(), 99);
	TEST_EQUAL(b3.get_bit(50), false);

	// Move construction and assignment on typed_bitfield
	typed_bitfield<int> tb1(64, true);
	typed_bitfield<int> tb2(std::move(tb1));
	TEST_EQUAL(tb2.size(), 64);
	TEST_EQUAL(tb2.all_set(), true);

	typed_bitfield<int> tb3;
	tb3 = std::move(tb2);
	TEST_EQUAL(tb3.size(), 64);
	TEST_EQUAL(tb3.all_set(), true);
}

TORRENT_TEST(swap_test)
{
	bitfield b1(30, true);
	bitfield b2(70, false);
	b2.set_bit(5);

	b1.swap(b2);
	TEST_EQUAL(b1.size(), 70);
	TEST_EQUAL(b1.count(), 1);
	TEST_EQUAL(b1.get_bit(5), true);

	TEST_EQUAL(b2.size(), 30);
	TEST_EQUAL(b2.count(), 30);
	TEST_EQUAL(b2.all_set(), true);
}

TORRENT_TEST(equality_operator)
{
	bitfield b1(65, false);
	bitfield b2(65, false);
	TEST_CHECK(b1 == b2);

	b1.set_bit(10);
	TEST_CHECK(!(b1 == b2));

	b2.set_bit(10);
	TEST_CHECK(b1 == b2);

	// Different size should never be equal
	bitfield b3(66, false);
	b3.set_bit(10);
	TEST_CHECK(!(b1 == b3));

	// Empty bitfields
	bitfield empty1;
	bitfield empty2(0);
	TEST_CHECK(empty1 == empty2);

	// Trailing padding bits differences should not cause false negatives
	std::uint8_t raw1[] = {0xff, 0xff};
	std::uint8_t raw2[] = {0xff, 0x80};
	bitfield b4(reinterpret_cast<char const*>(raw1), 9);
	bitfield b5(reinterpret_cast<char const*>(raw2), 9);
	TEST_CHECK(b4 == b5);
}

TORRENT_TEST(none_set_and_all_set_boundaries)
{
	// Test across various sizes around 32-bit word boundaries
	for (int size : {0, 1, 31, 32, 33, 63, 64, 65, 127, 128})
	{
		bitfield b(size, false);
		TEST_EQUAL(b.none_set(), true);
		if (size > 0)
		{
			TEST_EQUAL(b.all_set(), false);
			b.set_all();
			TEST_EQUAL(b.all_set(), true);
			TEST_EQUAL(b.none_set(), false);
			TEST_EQUAL(b.count(), size);

			b.clear_bit(size - 1);
			TEST_EQUAL(b.all_set(), false);
			TEST_EQUAL(b.count(), size - 1);
		}
		else
		{
			TEST_EQUAL(b.all_set(), false);
		}
	}
}

TORRENT_TEST(iterator_decrement_and_boundary_arithmetic)
{
	bitfield b(100, false);
	b.set_bit(0);
	b.set_bit(30);
	b.set_bit(34);
	b.set_bit(99);

	// Test operator-- decrement from end()
	auto it = b.end();
	int steps = 0;
	while (it != b.begin())
	{
		--it;
		++steps;
	}
	TEST_EQUAL(steps, 100);

	// Postfix vs prefix operations
	auto it2 = b.begin();
	auto it3 = it2++;
	TEST_CHECK(it3 == b.begin());
	TEST_CHECK(it2 == b.begin() + 1);

	auto it4 = it2--;
	TEST_CHECK(it4 == b.begin() + 1);
	TEST_CHECK(it2 == b.begin());

	// Crossing word boundaries with operator+ from a non-zero bit offset
	// Start at bit 30 (word 0, bit offset 30), add 34 -> should reach bit 64 (word 2, bit offset 0)
	auto it_30 = b.begin() + 30;
	auto it_64 = it_30 + 34;
	TEST_EQUAL(std::distance(b.begin(), it_64), 64);
	TEST_EQUAL(*it_30, true);

	auto it_34 = it_30 + 4;
	TEST_EQUAL(std::distance(b.begin(), it_34), 34);
	TEST_EQUAL(*it_34, true);

	// Const dereference check
	bitfield const& cb = b;
	bitfield::const_iterator cit = cb.begin();
	TEST_EQUAL(*cit, true);
}

TORRENT_TEST(data_pointers)
{
	bitfield empty_bf;
	TEST_CHECK(empty_bf.data() == nullptr);

	bitfield non_empty(10);
	TEST_CHECK(non_empty.data() != nullptr);

	non_empty.clear();
	TEST_CHECK(non_empty.data() == nullptr);
}
