/*

Copyright (c) 2024, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/visit_block_iovecs.hpp"
#include <array>
#include "test.hpp"

using lt::span;

namespace {

struct tbe
{
	span<char const> write_buf() const
	{
		return _buf;
	}
	span<char const> _buf;
};

template <size_t N>
tbe b(char const (&literal)[N])
{
	auto buf = span<char const>{&literal[0], N - 1};
	return tbe{buf};
}

std::string join(span<span<char const>> iovec)
{
	std::string ret;
	for (span<char const> const& b : iovec)
	{
		ret.append(b.begin(), b.end());
	}
	return ret;
}

}

TORRENT_TEST(visit_block_iovecs_full)
{
	std::array<tbe, 5> const blocks{b("a"), b("b"), b("c"), b("d"), b("e")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		TEST_EQUAL(cnt, 0);
		TEST_EQUAL(start_idx, 0);
		TEST_EQUAL(iovec.size(), 5);
		TEST_EQUAL(join(iovec), "abcde");
		++cnt;
		return false;
	});
}

TORRENT_TEST(visit_block_iovecs_one_hole)
{
	std::array<tbe, 5> const blocks{b("a"), b("b"), b(""), b("d"), b("e")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		switch (cnt) {
			case 0:
				TEST_EQUAL(start_idx, 0);
				TEST_EQUAL(iovec.size(), 2);
				TEST_EQUAL(join(iovec), "ab");
				break;
			case 1:
				TEST_EQUAL(start_idx, 3);
				TEST_EQUAL(iovec.size(), 2);
				TEST_EQUAL(join(iovec), "de");
				break;
			default:
				TORRENT_ASSERT_FAIL();
		}
		++cnt;
		return false;
	});
}

TORRENT_TEST(visit_block_iovecs_two_holes)
{
	std::array<tbe, 5> const blocks{b("a"), b(""), b("c"), b(""), b("e")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		switch (cnt) {
			case 0:
				TEST_EQUAL(start_idx, 0);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "a");
				break;
			case 1:
				TEST_EQUAL(start_idx, 2);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "c");
				break;
			case 2:
				TEST_EQUAL(start_idx, 4);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "e");
				break;
			default:
				TORRENT_ASSERT_FAIL();
		}
		++cnt;
		return false;
	});
}


TORRENT_TEST(visit_block_iovecs_interrupt)
{
	std::array<tbe, 3> const blocks{b("a"), b(""), b("c")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		switch (cnt) {
			case 0:
				TEST_EQUAL(start_idx, 0);
				TEST_EQUAL(iovec.size(), 1);
				TEST_EQUAL(join(iovec), "a");
				break;
			default:
				TORRENT_ASSERT_FAIL();
		}
		++cnt;
		return true;
	});
}

TORRENT_TEST(visit_block_iovecs_leading_hole)
{
	std::array<tbe, 5> const blocks{b(""), b("a"), b("b"), b("c"), b("d")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		TEST_EQUAL(cnt, 0);
		TEST_EQUAL(start_idx, 1);
		TEST_EQUAL(iovec.size(), 4);
		TEST_EQUAL(join(iovec), "abcd");
		++cnt;
		return false;
	});
}

TORRENT_TEST(visit_block_iovecs_trailing_hole)
{
	std::array<tbe, 5> const blocks{b("a"), b("b"), b("c"), b("d"), b("")};

	int cnt = 0;
	lt::aux::visit_block_iovecs(span<tbe const>(blocks)
		, [&cnt] (span<span<char const>> iovec, int start_idx) {
		TEST_EQUAL(cnt, 0);
		TEST_EQUAL(start_idx, 0);
		TEST_EQUAL(iovec.size(), 4);
		TEST_EQUAL(join(iovec), "abcd");
		++cnt;
		return false;
	});
}
