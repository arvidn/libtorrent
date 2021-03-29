/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/store_buffer.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size

using lt::aux::torrent_location;
using lt::aux::store_buffer;

namespace {

	char buf1;
	char buf2;
	char buf3;
	char buf4;

	lt::storage_index_t const st0(0);
	lt::storage_index_t const st1(1);
	lt::piece_index_t const p0(0);
	lt::piece_index_t const p1(1);

std::vector<torrent_location> build_locations()
{
	std::vector<torrent_location> ret;
	for (auto s : {st0, st1})
	{
		for (auto p : {p0, p1})
		{
			for (auto o : {0, lt::default_block_size})
			{
				ret.emplace_back(s, p, o);
			}
		}
	}
	return ret;
}

void check(store_buffer const& sb, torrent_location l, char const* expected)
{
	bool called = false;
	bool const ret = sb.get(l, [&](char const* buf) {
		TEST_EQUAL(buf, expected);
		called = true;
	});
	TEST_EQUAL(called, true);
	TEST_EQUAL(ret, true);
}

void check_miss(store_buffer const& sb, torrent_location l)
{
	int const ret = sb.get(l, [&](char const* b) {
		TEST_ERROR(b);
		return 1337;
	});
	TEST_EQUAL(ret, 0);
}

void check2(store_buffer const& sb, torrent_location l0, torrent_location l1
	, char const* expected0, char const* expected1)
{
	bool called = false;
	int const ret = sb.get2(l0, l1, [&](char const* b0, char const* b1) {
		TEST_EQUAL(b0, expected0);
		TEST_EQUAL(b1, expected1);
		called = true;
		return 1337;
	});
	TEST_EQUAL(called, true);
	TEST_EQUAL(ret, 1337);
}

void check2_miss(store_buffer const& sb, torrent_location l0, torrent_location l1)
{
	int const ret = sb.get2(l0, l1, [&](char const* b0, char const* b1) {
		TEST_ERROR(b0);
		TEST_ERROR(b1);
		return 1337;
	});
	TEST_EQUAL(ret, 0);
}

}

TORRENT_TEST(store_buffer_unique_keys)
{
	auto const locations = build_locations();
	store_buffer sb;
	// ensure all locations are independent
	for (auto l1 : locations)
	{
		sb.insert(l1, &buf1);
		for (auto l2 : locations)
		{
			if (l1 == l2)
				check(sb, l1, &buf1);
			else
				check_miss(sb, l2);
		}
		sb.erase(l1);
	}
}

TORRENT_TEST(store_buffer_get)
{
	auto const loc = build_locations();
	store_buffer sb;
	sb.insert(loc[0], &buf1);
	sb.insert(loc[1], &buf2);
	sb.insert(loc[2], &buf3);
	sb.insert(loc[3], &buf4);

	check(sb, loc[0], &buf1);
	check(sb, loc[1], &buf2);
	check(sb, loc[2], &buf3);
	check(sb, loc[3], &buf4);

	check_miss(sb, loc[4]);
	check_miss(sb, loc[5]);
	check_miss(sb, loc[6]);
	check_miss(sb, loc[7]);
}

TORRENT_TEST(store_buffer_get2)
{
	auto const loc = build_locations();
	store_buffer sb;
	sb.insert(loc[0], &buf1);
	sb.insert(loc[1], &buf2);
	sb.insert(loc[2], &buf3);
	sb.insert(loc[3], &buf4);

	// left side
	check2(sb, loc[0], loc[4], &buf1, nullptr);
	check2(sb, loc[1], loc[5], &buf2, nullptr);
	check2(sb, loc[2], loc[6], &buf3, nullptr);
	check2(sb, loc[3], loc[7], &buf4, nullptr);

	// right side
	check2(sb, loc[4], loc[0], nullptr, &buf1);
	check2(sb, loc[5], loc[1], nullptr, &buf2);
	check2(sb, loc[6], loc[2], nullptr, &buf3);
	check2(sb, loc[7], loc[3], nullptr, &buf4);

	// both sides
	check2(sb, loc[3], loc[0], &buf4, &buf1);
	check2(sb, loc[2], loc[1], &buf3, &buf2);
	check2(sb, loc[1], loc[2], &buf2, &buf3);
	check2(sb, loc[0], loc[3], &buf1, &buf4);

	// neither side
	check2_miss(sb, loc[4], loc[7]);
	check2_miss(sb, loc[5], loc[6]);
	check2_miss(sb, loc[6], loc[5]);
	check2_miss(sb, loc[7], loc[4]);
}

