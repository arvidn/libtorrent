/*

Copyright (c) 2012, Arvid Norberg
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
#include "libtorrent/aux_/merkle.hpp"

using namespace lt;

TORRENT_TEST(num_leafs)
{
	// test merkle_*() functions

	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14
	// num_leafs = 8

	TEST_EQUAL(merkle_num_leafs(1), 1);
	TEST_EQUAL(merkle_num_leafs(2), 2);
	TEST_EQUAL(merkle_num_leafs(3), 4);
	TEST_EQUAL(merkle_num_leafs(4), 4);
	TEST_EQUAL(merkle_num_leafs(5), 8);
	TEST_EQUAL(merkle_num_leafs(6), 8);
	TEST_EQUAL(merkle_num_leafs(7), 8);
	TEST_EQUAL(merkle_num_leafs(8), 8);
	TEST_EQUAL(merkle_num_leafs(9), 16);
	TEST_EQUAL(merkle_num_leafs(10), 16);
	TEST_EQUAL(merkle_num_leafs(11), 16);
	TEST_EQUAL(merkle_num_leafs(12), 16);
	TEST_EQUAL(merkle_num_leafs(13), 16);
	TEST_EQUAL(merkle_num_leafs(14), 16);
	TEST_EQUAL(merkle_num_leafs(15), 16);
	TEST_EQUAL(merkle_num_leafs(16), 16);
	TEST_EQUAL(merkle_num_leafs(17), 32);
	TEST_EQUAL(merkle_num_leafs(18), 32);
}

TORRENT_TEST(get_parent)
{
	// parents
	TEST_EQUAL(merkle_get_parent(1), 0);
	TEST_EQUAL(merkle_get_parent(2), 0);
	TEST_EQUAL(merkle_get_parent(3), 1);
	TEST_EQUAL(merkle_get_parent(4), 1);
	TEST_EQUAL(merkle_get_parent(5), 2);
	TEST_EQUAL(merkle_get_parent(6), 2);
	TEST_EQUAL(merkle_get_parent(7), 3);
	TEST_EQUAL(merkle_get_parent(8), 3);
	TEST_EQUAL(merkle_get_parent(9), 4);
	TEST_EQUAL(merkle_get_parent(10), 4);
	TEST_EQUAL(merkle_get_parent(11), 5);
	TEST_EQUAL(merkle_get_parent(12), 5);
	TEST_EQUAL(merkle_get_parent(13), 6);
	TEST_EQUAL(merkle_get_parent(14), 6);
}

TORRENT_TEST(get_sibling)
{
	// siblings
	TEST_EQUAL(merkle_get_sibling(1), 2);
	TEST_EQUAL(merkle_get_sibling(2), 1);
	TEST_EQUAL(merkle_get_sibling(3), 4);
	TEST_EQUAL(merkle_get_sibling(4), 3);
	TEST_EQUAL(merkle_get_sibling(5), 6);
	TEST_EQUAL(merkle_get_sibling(6), 5);
	TEST_EQUAL(merkle_get_sibling(7), 8);
	TEST_EQUAL(merkle_get_sibling(8), 7);
	TEST_EQUAL(merkle_get_sibling(9), 10);
	TEST_EQUAL(merkle_get_sibling(10), 9);
	TEST_EQUAL(merkle_get_sibling(11), 12);
	TEST_EQUAL(merkle_get_sibling(12), 11);
	TEST_EQUAL(merkle_get_sibling(13), 14);
	TEST_EQUAL(merkle_get_sibling(14), 13);
}

TORRENT_TEST(num_nodes)
{
	// total number of nodes given the number of leafs
	TEST_EQUAL(merkle_num_nodes(1), 1);
	TEST_EQUAL(merkle_num_nodes(2), 3);
	TEST_EQUAL(merkle_num_nodes(4), 7);
	TEST_EQUAL(merkle_num_nodes(8), 15);
	TEST_EQUAL(merkle_num_nodes(16), 31);
}

TORRENT_TEST(get_layer)
{
	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14

	TEST_EQUAL(merkle_get_layer(0), 0);
	TEST_EQUAL(merkle_get_layer(1), 1);
	TEST_EQUAL(merkle_get_layer(2), 1);
	TEST_EQUAL(merkle_get_layer(3), 2);
	TEST_EQUAL(merkle_get_layer(4), 2);
	TEST_EQUAL(merkle_get_layer(5), 2);
	TEST_EQUAL(merkle_get_layer(6), 2);
	TEST_EQUAL(merkle_get_layer(7), 3);
	TEST_EQUAL(merkle_get_layer(8), 3);
	TEST_EQUAL(merkle_get_layer(9), 3);
	TEST_EQUAL(merkle_get_layer(10), 3);
	TEST_EQUAL(merkle_get_layer(11), 3);
	TEST_EQUAL(merkle_get_layer(12), 3);
	TEST_EQUAL(merkle_get_layer(13), 3);
	TEST_EQUAL(merkle_get_layer(14), 3);
	TEST_EQUAL(merkle_get_layer(15), 4);
}
TORRENT_TEST(get_layer_offset)
{
	TEST_EQUAL(merkle_get_layer_offset(0), 0);
	TEST_EQUAL(merkle_get_layer_offset(1), 0);
	TEST_EQUAL(merkle_get_layer_offset(2), 1);
	TEST_EQUAL(merkle_get_layer_offset(3), 0);
	TEST_EQUAL(merkle_get_layer_offset(4), 1);
	TEST_EQUAL(merkle_get_layer_offset(5), 2);
	TEST_EQUAL(merkle_get_layer_offset(6), 3);
	TEST_EQUAL(merkle_get_layer_offset(7), 0);
	TEST_EQUAL(merkle_get_layer_offset(8), 1);
	TEST_EQUAL(merkle_get_layer_offset(9), 2);
	TEST_EQUAL(merkle_get_layer_offset(10), 3);
	TEST_EQUAL(merkle_get_layer_offset(11), 4);
	TEST_EQUAL(merkle_get_layer_offset(12), 5);
	TEST_EQUAL(merkle_get_layer_offset(13), 6);
	TEST_EQUAL(merkle_get_layer_offset(14), 7);
	TEST_EQUAL(merkle_get_layer_offset(15), 0);
}

