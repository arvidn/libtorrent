/*

Copyright (c) 2017, Arvid Norberg
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

