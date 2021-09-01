/*

Copyright (c) 2015, 2019-2020, Arvid Norberg
Copyright (c) 2017, Steven Siloti
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
#include <iostream>

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
	// total number of nodes given the number of leaves
	TEST_EQUAL(merkle_num_nodes(1), 1);
	TEST_EQUAL(merkle_num_nodes(2), 3);
	TEST_EQUAL(merkle_num_nodes(4), 7);
	TEST_EQUAL(merkle_num_nodes(8), 15);
	TEST_EQUAL(merkle_num_nodes(16), 31);
}

TORRENT_TEST(first_leaf)
{
	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14
	// total number of nodes given the number of leaves
	TEST_EQUAL(merkle_first_leaf(1), 0);
	TEST_EQUAL(merkle_first_leaf(2), 1);
	TEST_EQUAL(merkle_first_leaf(4), 3);
	TEST_EQUAL(merkle_first_leaf(8), 7);
	TEST_EQUAL(merkle_first_leaf(16), 15);
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
	// given a node index, how many steps from the left of the tree is that node?
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

TORRENT_TEST(merkle_num_layers)
{
	TEST_EQUAL(merkle_num_layers(0), 0);
	TEST_EQUAL(merkle_num_layers(1), 0);
	TEST_EQUAL(merkle_num_layers(2), 1);
	TEST_EQUAL(merkle_num_layers(4), 2);
	TEST_EQUAL(merkle_num_layers(8), 3);
	TEST_EQUAL(merkle_num_layers(16), 4);
}

TORRENT_TEST(merkle_get_first_child)
{
	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14
	TEST_EQUAL(merkle_get_first_child(0), 1);
	TEST_EQUAL(merkle_get_first_child(1), 3);
	TEST_EQUAL(merkle_get_first_child(2), 5);
	TEST_EQUAL(merkle_get_first_child(3), 7);
	TEST_EQUAL(merkle_get_first_child(4), 9);
	TEST_EQUAL(merkle_get_first_child(5), 11);
	TEST_EQUAL(merkle_get_first_child(6), 13);
	TEST_EQUAL(merkle_get_first_child(7), 15);
	TEST_EQUAL(merkle_get_first_child(8), 17);
	TEST_EQUAL(merkle_get_first_child(9), 19);
	TEST_EQUAL(merkle_get_first_child(10), 21);
	TEST_EQUAL(merkle_get_first_child(11), 23);
	TEST_EQUAL(merkle_get_first_child(12), 25);
	TEST_EQUAL(merkle_get_first_child(13), 27);
	TEST_EQUAL(merkle_get_first_child(14), 29);
	TEST_EQUAL(merkle_get_first_child(15), 31);
	TEST_EQUAL(merkle_get_first_child(16), 33);
}

TORRENT_TEST(merkle_get_first_child2)
{
	// this is the structure:
	//                        0
	//            1                      2
	//      3           4           5           6
	//   7     8     9     10    11    12    13    14
	// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30
	// 31 ...

	TEST_EQUAL(merkle_get_first_child(0, 0), 0);
	TEST_EQUAL(merkle_get_first_child(0, 1), 1);
	TEST_EQUAL(merkle_get_first_child(0, 2), 3);
	TEST_EQUAL(merkle_get_first_child(0, 3), 7);
	TEST_EQUAL(merkle_get_first_child(0, 4), 15);
	TEST_EQUAL(merkle_get_first_child(0, 5), 31);

	TEST_EQUAL(merkle_get_first_child(2, 0), 2);
	TEST_EQUAL(merkle_get_first_child(2, 1), 5);
	TEST_EQUAL(merkle_get_first_child(2, 2), 11);
	TEST_EQUAL(merkle_get_first_child(2, 3), 23);
	TEST_EQUAL(merkle_get_first_child(2, 4), 47);
	TEST_EQUAL(merkle_get_first_child(2, 5), 95);
}

TORRENT_TEST(merkle_layer_start)
{
	TEST_EQUAL(merkle_layer_start(0), 0);
	TEST_EQUAL(merkle_layer_start(1), 1);
	TEST_EQUAL(merkle_layer_start(2), 3);
	TEST_EQUAL(merkle_layer_start(3), 7);
	TEST_EQUAL(merkle_layer_start(4), 15);
	TEST_EQUAL(merkle_layer_start(5), 31);
	TEST_EQUAL(merkle_layer_start(6), 63);
	TEST_EQUAL(merkle_layer_start(7), 127);
	TEST_EQUAL(merkle_layer_start(8), 255);
	TEST_EQUAL(merkle_layer_start(9), 511);
}

TORRENT_TEST(merkle_to_flat_index)
{
	TEST_EQUAL(merkle_to_flat_index(0, 0), 0);
	TEST_EQUAL(merkle_to_flat_index(1, 0), 1);
	TEST_EQUAL(merkle_to_flat_index(1, 1), 2);
	TEST_EQUAL(merkle_to_flat_index(2, 0), 3);
	TEST_EQUAL(merkle_to_flat_index(2, 1), 4);
	TEST_EQUAL(merkle_to_flat_index(2, 2), 5);
	TEST_EQUAL(merkle_to_flat_index(2, 3), 6);
	TEST_EQUAL(merkle_to_flat_index(3, 0), 7);
	TEST_EQUAL(merkle_to_flat_index(3, 1), 8);
	TEST_EQUAL(merkle_to_flat_index(3, 2), 9);
	TEST_EQUAL(merkle_to_flat_index(3, 3), 10);
	TEST_EQUAL(merkle_to_flat_index(3, 4), 11);
	TEST_EQUAL(merkle_to_flat_index(3, 5), 12);
	TEST_EQUAL(merkle_to_flat_index(3, 6), 13);
	TEST_EQUAL(merkle_to_flat_index(3, 7), 14);
}

namespace {
sha256_hash H(sha256_hash left, sha256_hash right)
{
	hasher256 st;
	st.update(left);
	st.update(right);
	return st.final();
}
}

using v = std::vector<sha256_hash>;
sha256_hash const a("11111111111111111111111111111111");
sha256_hash const b("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
sha256_hash const c("cccccccccccccccccccccccccccccccc");
sha256_hash const d("dddddddddddddddddddddddddddddddd");
sha256_hash const e("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
sha256_hash const f("ffffffffffffffffffffffffffffffff");
sha256_hash const g("gggggggggggggggggggggggggggggggg");
sha256_hash const h("iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii");

// 0 and 1
sha256_hash const o;
sha256_hash const l("11111111111111111111111111111111");

// combinations
sha256_hash const ab = H(a,b);
sha256_hash const cd = H(c,d);
sha256_hash const ef = H(e,f);
sha256_hash const gh = H(g,h);

sha256_hash const ad = H(ab,cd);
sha256_hash const eh = H(ef,gh);

sha256_hash const ah = H(ad,eh);

TORRENT_TEST(merkle_fill_tree)
{
	// fill whole tree
	{
		v tree{
		       o,
		   o,      o,
		  o, o,   o, o,
		a,b,c,d,e,f,g,h};

		merkle_fill_tree(tree, 8, 7);

		TEST_CHECK((tree ==
		     v{
		       ah,
		   ad,     eh,
		 ab, cd, ef, gh,
		a,b,c,d,e,f,g,h}));
	}

	// fill left side of the tree
	{
		v tree{
		       o,
		   o,      o,
		 ab,cd,   o, o,
		a,b,c,d,o,o,o,o};

		merkle_fill_tree(tree, 4, 7);

		TEST_CHECK((tree ==
		     v{o,
		   ad,     o,
		 ab, cd, o, o,
		a,b,c,d,o,o,o,o}));
	}

	// fill right side of the tree
	{
		v tree{
		       o,
		   o,      o,
		 o,  o,   o, o,
		o,o,o,o,a,b,c,d};

		merkle_fill_tree(tree, 4, 11);

		TEST_CHECK((tree ==
		     v{o,
		   o,      ad,
		 o,  o,  ab,cd,
		o,o,o,o,a,b,c,d}));
	}

	// fill shallow left of the tree
	{
		v tree{
		       o,
		   o,      o,
		 a,  b,   o, o,
		o,o,o,o,o,o,o,o};

		merkle_fill_tree(tree, 2, 3);

		TEST_CHECK((tree ==
		     v{o,
		   ab,     o,
		 a,  b,   o, o,
		o,o,o,o,o,o,o,o}));
	}

	// fill shallow right of the tree
	{
		v tree{
		       o,
		   o,      o,
		 o,  o,   a, b,
		o,o,o,o,o,o,o,o};

		merkle_fill_tree(tree, 2, 5);

		TEST_CHECK((tree ==
		     v{o,
		   o,      ab,
		 o,  o,   a, b,
		o,o,o,o,o,o,o,o}));
	}
}

TORRENT_TEST(merkle_fill_partial_tree)
{
	// fill whole tree
	{
		v tree{o,
		   o,      o,
		  o, o,   o, o,
		a,b,c,d,e,f,g,h};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 ab, cd, ef, gh,
		a,b,c,d,e,f,g,h}));
	}

	// fill left side of the tree
	{
		v tree{o,
		   o,      eh,
		 ab,cd,   o, o,
		a,b,c,d,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,    eh,
		 ab, cd, o, o,
		a,b,c,d,o,o,o,o}));
	}

	// fill right side of the tree
	{
		v tree{o,
		   ad,     o,
		 o,  o,   o, o,
		o,o,o,o,e,f,g,h};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 o,  o,  ef,gh,
		o,o,o,o,e,f,g,h}));
	}

	// fill shallow left of the tree
	{
		v tree{
		       o,
		   o,      eh,
		 ab, cd,   o, o,
		o,o,o,o,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,    eh,
		 ab, cd,   o, o,
		o,o,o,o,o,o,o,o}));
	}

	// fill shallow right of the tree
	{
		v tree{
		       o,
		   ad,     o,
		 o,  o,   ef,gh,
		o,o,o,o,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 o,  o,   ef, gh,
		o,o,o,o,o,o,o,o}));
	}

	// fill uneven tree
	{
		v tree{
		       o,
		   ad,     o,
		 o,  o,  ef, gh,
		o,o,o,o,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 o,  o,  ef, gh,
		o,o,o,o,o,o,o,o}));
	}

	// clear orphans
	{
		v tree{
		       o,
		   ad,    ah,
		 o,  o,  ef, gh,
		a,o,c,o,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 o,  o,   ef,gh,
		o,o,o,o,o,o,o,o}));
	}

	// clear orphan sub-tree
	{
		v tree{o,
		   o,     o,
		 o, o,   o, o,
		a,b,c,d,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		    v{o,
		   o,     o,
		 o, o,   o, o,
		o,o,o,o,o,o,o,o}));
	}

	// fill sub-tree
	{
		v tree{o,
		   o,     eh,
		 o, o,   o, o,
		a,b,c,d,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		    v{ah,
		   ad,   eh,
		 ab,cd,   o, o,
		a,b,c,d,o,o,o,o}));
	}

	// clear no-siblings left
	{
		v tree{
		       o,
		   ad,    ah,
		 o,  o,  ef, gh,
		o,o,o,o,o,o,o,h};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 o,  o,  ef, gh,
		o,o,o,o,o,o,o,o}));
	}

	// clear no-siblings right
	{
		v tree{
		       o,
		   ad,    ah,
		 o,  o,  ef, gh,
		o,o,o,o,o,o,g,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 o,  o,  ef, gh,
		o,o,o,o,o,o,o,o}));
	}

	// fill gaps
	{
		v tree{
		       o,
		   ad,    ah,
		 o,  o,  ef,gh,
		a,b,c,d,o,o,o,o};

		merkle_fill_partial_tree(tree);

		TEST_CHECK((tree ==
		     v{ah,
		   ad,     eh,
		 ab,cd,   ef,gh,
		a,b,c,d,o,o,o,o}));
	}
}

TORRENT_TEST(merkle_root)
{
	// all leaves in the tree
	TEST_CHECK(merkle_root(v{a,b,c,d,e,f,g,h}, o) == ah);

	// not power-of-two number of leaves
	TEST_CHECK(merkle_root(v{a,b,c,d,e,f}, o) == H(ad, H(ef, H(o, o))));

	// very small tree
	TEST_CHECK(merkle_root(v{a,b}, o) == ab);

	// single hash-tree
	TEST_CHECK(merkle_root(v{a}) == a);
}

TORRENT_TEST(merkle_root_scratch)
{
	std::vector<sha256_hash> buf;

	// all leaves in the tree
	TEST_CHECK(merkle_root_scratch(v{a,b,c,d,e,f,g,h}, 8, o, buf) == ah);

	// not power-of-two number of leaves
	TEST_CHECK(merkle_root_scratch(v{a,b,c,d,e,f}, 8, o, buf) == H(ad, H(ef, H(o, o))));

	// very small tree
	TEST_CHECK(merkle_root_scratch(v{a,b}, 2, o, buf) == ab);

	// unaligned leaf layer
	TEST_CHECK(merkle_root_scratch(v{a,b,c}, 8, o, buf) == H(H(ab, H(c, o)), H(H(o,o), H(o,o))));
}

namespace {
void print_tree(span<sha256_hash const> tree)
{
	int const num_leafs = static_cast<int>((tree.size() + 1) / 2);
	int spacing = num_leafs;
	int const num_levels = merkle_num_layers(num_leafs) + 1;
	int layer_width = 1;
	int node = 0;
	for (int i = 0; i < num_levels; ++i)
	{
		for (int k = 0; k < layer_width; ++k)
		{
			for (int j = 0; j < spacing; ++j) std::cout << ' ';
			std::cout << (tree[node] == sha256_hash() ? '0' : '1');
			for (int j = 0; j < spacing - 1; ++j) std::cout << ' ';
			++node;
		}
		std::cout << '\n';
		layer_width *= 2;
		spacing /= 2;
	}
	std::cout << '\n';
}
}

TORRENT_TEST(merkle_clear_tree)
{
	// test clearing the whole tree
	{
		v tree{l,
		   l,      l,
		  l, l,   l, l,
		l,l,l,l,l,l,l,l};

		print_tree(tree);
		merkle_clear_tree(tree, 8, 7);
		print_tree(tree);

		TEST_CHECK((tree ==
		     v{o,
		   o,      o,
		  o, o,   o, o,
		o,o,o,o,o,o,o,o}));
	}

	// test clearing the left side of the tree
	{
		v tree{l,
		   l,      l,
		  l, l,   l, l,
		l,l,l,l,l,l,l,l};

		print_tree(tree);
		merkle_clear_tree(tree, 4, 7);
		print_tree(tree);

		TEST_CHECK((tree ==
		     v{l,
		   o,      l,
		  o, o,   l, l,
		o,o,o,o,l,l,l,l}));
	}

	// test clearing the right side of the tree
	{
		v tree{l,
		   l,      l,
		  l, l,   l, l,
		l,l,l,l,l,l,l,l};

		print_tree(tree);
		merkle_clear_tree(tree, 4, 11);
		print_tree(tree);

		TEST_CHECK((tree ==
		     v{l,
		   l,      o,
		  l, l,   o, o,
		l,l,l,l,o,o,o,o}));
	}

	// test clearing shallow left
	{
		v tree{l,
		   l,      l,
		  l, l,   l, l,
		l,l,l,l,l,l,l,l};

		print_tree(tree);
		merkle_clear_tree(tree, 2, 3);
		print_tree(tree);

		TEST_CHECK((tree ==
		     v{l,
		   o,      l,
		  o, o,   l, l,
		l,l,l,l,l,l,l,l}));
	}

	// test clearing shallow right
	{
		v tree{l,
		   l,      l,
		  l, l,   l, l,
		l,l,l,l,l,l,l,l};

		print_tree(tree);
		merkle_clear_tree(tree, 2, 5);
		print_tree(tree);

		TEST_CHECK((tree ==
		     v{l,
		   l,      o,
		  l, l,   o, o,
		l,l,l,l,l,l,l,l}));
	}
}

TORRENT_TEST(merkle_pad)
{
	// if the block layer is the same as the piece layer, the pad is always just
	// zeroes
	TEST_CHECK(merkle_pad(1, 1) == sha256_hash{});
	TEST_CHECK(merkle_pad(2, 2) == sha256_hash{});
	TEST_CHECK(merkle_pad(4, 4) == sha256_hash{});
	TEST_CHECK(merkle_pad(8, 8) == sha256_hash{});
	TEST_CHECK(merkle_pad(16, 16) == sha256_hash{});

	// if the block layer is one step below the piece layer, the pad is always
	// SHA256(0 .. 0). i.e. two zero hashes hashed.

	auto const pad1 = [] {
		hasher256 ctx;
		ctx.update(sha256_hash{});
		ctx.update(sha256_hash{});
		return ctx.final();
	}();
	TEST_CHECK(merkle_pad(2, 1) == pad1);
	TEST_CHECK(merkle_pad(4, 2) == pad1);
	TEST_CHECK(merkle_pad(8, 4) == pad1);
	TEST_CHECK(merkle_pad(16, 8) == pad1);

	auto const pad2 = [&] {
		hasher256 ctx;
		ctx.update(pad1);
		ctx.update(pad1);
		return ctx.final();
	}();
	TEST_CHECK(merkle_pad(4, 1) == pad2);
	TEST_CHECK(merkle_pad(8, 2) == pad2);
	TEST_CHECK(merkle_pad(16, 4) == pad2);
	TEST_CHECK(merkle_pad(32, 8) == pad2);
}

TORRENT_TEST(merkle_validate_node)
{
	TEST_CHECK(merkle_validate_node(a, b, ab));
	TEST_CHECK(merkle_validate_node(c, d, cd));
	TEST_CHECK(merkle_validate_node(e, f, ef));
	TEST_CHECK(merkle_validate_node(g, h, gh));

	TEST_CHECK(merkle_validate_node(ab, cd, ad));
	TEST_CHECK(merkle_validate_node(ef, gh, eh));

	TEST_CHECK(merkle_validate_node(ad, eh, ah));

	TEST_CHECK(!merkle_validate_node(b, a, ab));
	TEST_CHECK(!merkle_validate_node(d, c, cd));
	TEST_CHECK(!merkle_validate_node(f, e, ef));
	TEST_CHECK(!merkle_validate_node(h, g, gh));
}

TORRENT_TEST(merkle_validate_copy_full)
{

	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,c,d,e,f,g,h};

	v empty_tree(15);

	merkle_validate_copy(src, empty_tree, ah);

	TEST_CHECK(empty_tree == src);
}

TORRENT_TEST(merkle_validate_copy_partial)
{

	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, o,
	a,b,c,o,o,o,o,o};

	v empty_tree(15);

	merkle_validate_copy(src, empty_tree, ah);

	v const expected{
	       ah,
	   ad,     eh,
	 ab, cd,  o, o,
	a,b,o,o,o,o,o,o};

	TEST_CHECK(empty_tree == expected);
}

TORRENT_TEST(merkle_validate_copy_invalid_root)
{

	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, o,
	a,b,c,o,o,o,o,o};

	v empty_tree(15);

	merkle_validate_copy(src, empty_tree, a);

	v const expected(15);

	TEST_CHECK(empty_tree == expected);
}

TORRENT_TEST(merkle_validate_copy_root_only)
{

	v const src{
	       ah,
	    o,      o,
	  o,  o,  o, o,
	o,o,o,o,o,o,o,o};

	v empty_tree(15);

	merkle_validate_copy(src, empty_tree, ah);

	v const expected{
	       ah,
	    o,      o,
	  o,  o,  o, o,
	o,o,o,o,o,o,o,o};

	TEST_CHECK(empty_tree == expected);
}

TORRENT_TEST(merkle_validate_single_leayer_fail_no_parents)
{
	v const src{
	        o,
	    o,      o,
	  o,  o,  o, o,
	a,b,c,d,e,f,g,h};

	TEST_CHECK(!merkle_validate_single_layer(src));
}

TORRENT_TEST(merkle_validate_single_layer_missing_parent)
{
	v const src{
	        o,
	    o,      o,
	  ab, cd,  o,gh,
	a,b,c,d,e,f,g,h};

	TEST_CHECK(!merkle_validate_single_layer(src));
}

TORRENT_TEST(merkle_validate_single_layer_missing_leaf)
{
	v const src{
	        o,
	    o,      o,
	  ab, cd, ef,gh,
	a,b,c,o,e,f,g,h};

	TEST_CHECK(!merkle_validate_single_layer(src));
}

TORRENT_TEST(merkle_validate_single_layer)
{
	v const src{
	        o,
	    o,      o,
	  ab, cd, ef,gh,
	a,b,c,d,e,f,g,h};

	TEST_CHECK(merkle_validate_single_layer(src));
}

TORRENT_TEST(is_subtree_known_full)
{
	v const src{
	        ah,
	    ad,     eh,
	  ab, cd, ef,gh,
	a,b,c,d,e,f,g,h};

	TEST_CHECK(merkle_find_known_subtree(src, 1, 8) == std::make_tuple(0, 2, 3));
}

TORRENT_TEST(is_subtree_known_two_levels)
{
	v const src{
	        ah,
	    ad,     eh,
	  o, o, ef,gh,
	a,b,c,d,e,f,g,h};

	TEST_CHECK(merkle_find_known_subtree(src, 1, 8) == std::make_tuple(0, 4, 1));
}

TORRENT_TEST(is_subtree_known_unknown)
{
	v const src{
	        ah,
	    ad,     eh,
	  o, o, ef,gh,
	a,b,o,d,e,f,g,h};

	TEST_CHECK(merkle_find_known_subtree(src, 1, 8) == std::make_tuple(0, 2, 3));
}

TORRENT_TEST(is_subtree_known_padding)
{
	// the last leaf is padding, it should be assumed to be correct despite
	// being zero
	v const src{
	        ah,
	    ad,     eh,
	  o, o, ef,gh,
	a,b,o,d,e,f,g,o};

	TEST_CHECK(merkle_find_known_subtree(src, 6, 7) == std::make_tuple(6, 2, 6));
}

TORRENT_TEST(is_subtree_known_padding_two_levels)
{
	// the last leaf is padding, it should be assumed to be correct despite
	// being zero
	v const src{
	        ah,
	    ad,     eh,
	  o, o,  o, o,
	a,b,o,d,e,f,g,o};

	TEST_CHECK(merkle_find_known_subtree(src, 6, 7) == std::make_tuple(4, 4, 2));
}

TORRENT_TEST(is_subtree_known_more_padding_two_levels)
{
	// the last two leafs are padding, they should be assumed to be correct despite
	// being zero
	v const src{
	        ah,
	    ad,     eh,
	  o, o,  o, o,
	a,b,o,d,e,f,o,o};

	TEST_CHECK(merkle_find_known_subtree(src, 5, 6) == std::make_tuple(4, 4, 2));
}

TORRENT_TEST(validate_and_insert_proofs_mixed)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs{f, gh, ad};

	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 11, e, proofs));
	TEST_CHECK((tree == v{
	          ah,
	     ad,       eh,
	  o,    o,  ef,   gh,
	o, o, o, o,e, f, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_mixed_failure)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = eh; // this is not the correct root

	v const proofs{f, gh, ad};

	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 11, e, proofs));

	// make sure all nodes that were filled in were cleared correctly
	TEST_CHECK((tree == v{
	          eh,
	      o,        o,
	  o,    o,   o,    o,
	o, o, o, o,o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_left)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs{b, cd, eh};

	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 7, a, proofs));
	TEST_CHECK((tree == v{
	          ah,
	     ad,       eh,
	  ab,   cd,  o,    o,
	a, b, o, o,o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_right)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs{g, ef, ad};

	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 14, h, proofs));
	TEST_CHECK((tree == v{
	          ah,
	     ad,       eh,
	  o,    o,   ef,   gh,
	o, o, o, o,o, o, g, h}));
}

TORRENT_TEST(validate_and_insert_proofs_early_success)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;
	tree[1] = ad;
	tree[2] = eh;

	v const proofs{f, gh, ad};

	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 11, e, proofs));
	TEST_CHECK((tree == v{
	          ah,
	     ad,       eh,
	  o,    o,  ef,   gh,
	o, o, o, o,e, f, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_early_failure)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;
	tree[1] = ad;
	tree[2] = ah; // <- this is not the right hash. It should cause validation to fail

	v const proofs{f, gh, ad};

	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 11, e, proofs));

	// make sure tree was correctly restored
	TEST_CHECK((tree == v{
	          ah,
	     ad,       ah,
	  o,    o,   o,    o,
	o, o, o, o,o, o, o, o}));
}


TORRENT_TEST(validate_and_insert_proofs_no_uncles)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs;

	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 1, ad, proofs));

	// make sure tree was correctly restored
	TEST_CHECK((tree == v{
	          ah,
	      o,        o,
	  o,    o,   o,    o,
	o, o, o, o,o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_root)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs;

	// this is just attempting to prove the root, which is ok
	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 0, ah, proofs));

	// nothing happens to the tree in this case, we already had the root
	TEST_CHECK((tree == v{
	          ah,
	      o,        o,
	  o,    o,   o,    o,
	o, o, o, o,o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_root_fail)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs;

	// this is just attempting to prove the root, but with the wrong hash
	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 0, a, proofs));

	// nothing happens to the tree in this case
	TEST_CHECK((tree == v{
	          ah,
	      o,        o,
	  o,    o,   o,    o,
	o, o, o, o,o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_too_many_uncles)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

	v tree(15);
	tree[0] = ah;

	v const proofs{f, gh, ad, a, b, c , d};

	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 11, e, proofs));
	TEST_CHECK((tree == v{
	          ah,
	     ad,       eh,
	  o,    o,  ef,   gh,
	o, o, o, o,e, f, o, o}));
}
