/*

Copyright (c) 2021, Alden Torres
Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"

#include "libtorrent/aux_/vector_utils.hpp"

#include <vector>
#include <algorithm>

using namespace lt;

TORRENT_TEST(sorted_find_small)
{
	std::vector<int> v = {1, 2, 3, 4, 5, 6};

	// find an existing element
	auto it = aux::sorted_find(v, 4);
	TEST_EQUAL(std::distance(v.begin(), it), 3);

	// find a non-existing element
	auto it_unknown = aux::sorted_find(v, 8);
	TEST_CHECK(it_unknown == v.end());
}

TORRENT_TEST(sorted_insert_small)
{
	std::vector<int> v = {1, 2, 3, 4, 5, 6};

	aux::sorted_insert(v, 4);
	TEST_CHECK(v == std::vector<int>({1, 2, 3, 4, 4, 5, 6}));

	aux::sorted_insert(v, 0);
	TEST_CHECK(v == std::vector<int>({0, 1, 2, 3, 4, 4, 5, 6}));

	aux::sorted_insert(v, 10);
	TEST_CHECK(v == std::vector<int>({0, 1, 2, 3, 4, 4, 5, 6, 10}));
}
