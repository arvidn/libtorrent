/*

Copyright (c) 2017-2018, Alden Torres
Copyright (c) 2017-2022, Arvid Norberg
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"

#include <vector>
#include <array>

using namespace lt;

namespace {

span<char const> f(span<char const> x) { return x; }
span<span<char>> g(span<span<char>> x) { return x; }

} // anonymous namespace

TORRENT_TEST(span_vector)
{
	std::vector<char> v1 = {1,2,3,4};
	span<char> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_vector_assignment)
{
	std::vector<char> v1 = {1,2,3,4};
	span<char> a;
	a = v1;
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_assignment)
{
	char v1[] = {1,2,3,4};
	span<char> a2(v1);
	span<char> a;
	a = a2;
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

namespace {

void do_span_temp_vector(span<char const> a)
{
	std::vector<char> v1 = {1,2,3,4};
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

} // anonymous namespace

TORRENT_TEST(span_temp_vector)
{
	do_span_temp_vector(std::vector<char>{1,2,3,4});
}

TORRENT_TEST(span_std_array)
{
	std::array<char, 4> v1{{1,2,3,4}};
	span<char> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_const_std_array)
{
	std::array<char const, 4> v1{{1,2,3,4}};
	span<char const> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_array)
{
	char v1[] = {1,2,3,4};
	span<char> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_string)
{
	std::string v1 = "test";
	span<char const> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_const_array)
{
	char const v1[] = {1,2,3,4};
	span<char const> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 4);
}

TORRENT_TEST(span_single_element)
{
	char const v1 = 1;
	span<char const> a(v1);
	TEST_CHECK(a == f(v1));
	TEST_CHECK(a.size() == 1);
}

TORRENT_TEST(span_of_spans)
{
	std::vector<char> v1 = {1,2,3,4};
	span<char> s1(v1);
	span<span<char>> a(s1);
	TEST_CHECK(a == g(s1));
	TEST_CHECK(a.size() == 1);
	TEST_CHECK(a[0].size() == 4);
}

