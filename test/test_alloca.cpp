/*

Copyright (c) 2017, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/alloca.hpp"

using namespace lt;

namespace {

struct A
{
	int val = 1337;
};

int destructed = 0;

struct B
{
	~B() { ++destructed; }
};

}

TORRENT_TEST(alloca_construct)
{
	TORRENT_ALLOCA(vec, A, 13);

	TEST_EQUAL(vec.size(), 13);
	for (auto const& o : vec)
	{
		TEST_EQUAL(o.val, 1337);
	}
}

TORRENT_TEST(alloca_destruct)
{
	{
		destructed = 0;
		TORRENT_ALLOCA(vec, B, 3);
	}
	TEST_EQUAL(destructed, 3);
}

TORRENT_TEST(alloca_large)
{
	// this is something like 256 kiB of allocation
	// it should be made on the heap and always succeed
	TORRENT_ALLOCA(vec, A, 65536);
	for (auto const& a : vec)
		TEST_EQUAL(a.val, 1337);
}

