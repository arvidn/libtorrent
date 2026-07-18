/*

Copyright (c) 2020, Alden Torres
Copyright (c) 2015, 2019-2021, Arvid Norberg
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/hasher.hpp"
#include <iostream>

using namespace lt;

namespace {

	void compare_bits(bitfield const& bits, char const* str)
	{
		for (int i = 0; *str; ++i, ++str)
		{
			if (*str == '1')
				TEST_CHECK(bits.get_bit(i));
			else if (*str == '0')
				TEST_CHECK(!bits.get_bit(i));
			else
				TEST_CHECK(false);
		}
	}

	// Construct an aux::merkle_tree pre-loaded with the hashes from a
	// hand-laid-out flat std::vector (standard layout: root at index 0,
	// layer L at [(1<<L) - 1, (1<<(L+1)) - 1)). The tree's root is taken
	// from src[0]; the tree is transitioned into full_tree mode so the
	// tests can address nodes via (L, O) directly.
	aux::merkle_tree make_tree(std::vector<sha256_hash> const& src, int const blocks_per_piece = 1)
	{
		int const num_nodes = int(src.size());
		TORRENT_ASSERT(((num_nodes + 1) & num_nodes) == 0);
		int const num_leafs = (num_nodes + 1) / 2;
		aux::merkle_tree t(num_leafs, blocks_per_piece, src[0].data());
		t.allocate_compact();
		int const N = t.num_layers();
		for (int L = 0; L <= N; ++L)
		{
			int const start = (1 << L) - 1;
			int const layer_size = 1 << L;
			for (int O = 0; O < layer_size; ++O)
				t.set(L, O, src[start + O]);
		}
		return t;
	}

	// Read back the tree's contents into a standard-layout flat vector.
	std::vector<sha256_hash> flatten(aux::merkle_tree const& t)
	{
		int const N = t.num_layers();
		std::vector<sha256_hash> out(std::size_t(merkle_num_nodes(1 << N)));
		for (int L = 0; L <= N; ++L)
		{
			int const start = (1 << L) - 1;
			int const layer_size = 1 << L;
			for (int O = 0; O < layer_size; ++O)
				out[start + O] = t.get(L, O);
		}
		return out;
	}
}

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
		auto t = make_tree(v{o, o, o, o, o, o, o, a, b, c, d, e, f, g, h});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, ab, cd, ef, gh, a, b, c, d, e, f, g, h}));
	}

	// fill left side of the tree
	{
		auto t = make_tree(v{o, o, eh, ab, cd, o, o, a, b, c, d, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, ab, cd, o, o, a, b, c, d, o, o, o, o}));
	}

	// fill right side of the tree
	{
		auto t = make_tree(v{o, ad, o, o, o, o, o, o, o, o, o, e, f, g, h});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, e, f, g, h}));
	}

	// fill shallow left of the tree
	{
		auto t = make_tree(v{o, o, eh, ab, cd, o, o, o, o, o, o, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, ab, cd, o, o, o, o, o, o, o, o, o, o}));
	}

	// fill shallow right of the tree
	{
		auto t = make_tree(v{o, ad, o, o, o, ef, gh, o, o, o, o, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, o, o, o, o}));
	}

	// fill uneven tree
	{
		auto t = make_tree(v{o, ad, o, o, o, ef, gh, o, o, o, o, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, o, o, o, o}));
	}

	// clear orphans
	{
		auto t = make_tree(v{o, ad, ah, o, o, ef, gh, a, o, c, o, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, o, o, o, o}));
	}

	// clear orphan sub-tree
	{
		auto t = make_tree(v{o, o, o, o, o, o, o, a, b, c, d, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{o, o, o, o, o, o, o, o, o, o, o, o, o, o, o}));
	}

	// fill sub-tree
	{
		auto t = make_tree(v{o, o, eh, o, o, o, o, a, b, c, d, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, ab, cd, o, o, a, b, c, d, o, o, o, o}));
	}

	// clear no-siblings left
	{
		auto t = make_tree(v{o, ad, ah, o, o, ef, gh, o, o, o, o, o, o, o, h});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, o, o, o, o}));
	}

	// clear no-siblings right
	{
		auto t = make_tree(v{o, ad, ah, o, o, ef, gh, o, o, o, o, o, o, g, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, o, o, o, o}));
	}

	// fill gaps
	{
		auto t = make_tree(v{o, ad, ah, o, o, ef, gh, a, b, c, d, o, o, o, o});

		merkle_fill_partial_tree(t);

		TEST_CHECK((flatten(t) == v{ah, ad, eh, ab, cd, ef, gh, a, b, c, d, o, o, o, o}));
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

TORRENT_TEST(merkle_clear_tree)
{
	// test clearing the whole tree
	{
		auto t = make_tree(v{l, l, l, l, l, l, l, l, l, l, l, l, l, l, l});

		// level_start=7 -> (L=3, O=0), num_leafs=8
		merkle_clear_tree(t, 3, 0, 8);

		TEST_CHECK((flatten(t) == v{o, o, o, o, o, o, o, o, o, o, o, o, o, o, o}));
	}

	// test clearing the left side of the tree
	{
		auto t = make_tree(v{l, l, l, l, l, l, l, l, l, l, l, l, l, l, l});

		// level_start=7 -> (L=3, O=0), num_leafs=4
		merkle_clear_tree(t, 3, 0, 4);

		TEST_CHECK((flatten(t) == v{l, o, l, o, o, l, l, o, o, o, o, l, l, l, l}));
	}

	// test clearing the right side of the tree
	{
		auto t = make_tree(v{l, l, l, l, l, l, l, l, l, l, l, l, l, l, l});

		// level_start=11 -> (L=3, O=4), num_leafs=4
		merkle_clear_tree(t, 3, 4, 4);

		TEST_CHECK((flatten(t) == v{l, l, o, l, l, o, o, l, l, l, l, o, o, o, o}));
	}

	// test clearing shallow left
	{
		auto t = make_tree(v{l, l, l, l, l, l, l, l, l, l, l, l, l, l, l});

		// level_start=3 -> (L=2, O=0), num_leafs=2
		merkle_clear_tree(t, 2, 0, 2);

		TEST_CHECK((flatten(t) == v{l, o, l, o, o, l, l, l, l, l, l, l, l, l, l}));
	}

	// test clearing shallow right
	{
		auto t = make_tree(v{l, l, l, l, l, l, l, l, l, l, l, l, l, l, l});

		// level_start=5 -> (L=2, O=2), num_leafs=2
		merkle_clear_tree(t, 2, 2, 2);

		TEST_CHECK((flatten(t) == v{l, l, o, l, l, o, o, l, l, l, l, l, l, l, l}));
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

namespace {
	// Construct an empty (root-only) merkle_tree for use as the dst in
	// merkle_validate_copy tests. The root is set to zero so that flatten()
	// returns all-zeros when no copy happens (mismatched expected_root case).
	aux::merkle_tree make_empty_dst(int num_blocks, sha256_hash const& root_buf)
	{
		aux::merkle_tree dst(num_blocks, 1, root_buf.data());
		dst.allocate_compact();
		// allocate_compact set position (0,0) to dst's root; zero it so the
		// merkle_validate_copy walk's set(0,0,src[0]) is the only write there.
		dst.set(0, 0, sha256_hash{});
		return dst;
	}
}

TORRENT_TEST(merkle_validate_copy_full)
{
	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,c,d,e,f,g,h};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	bitfield verified(8);

	merkle_validate_copy(src, dst, ah, verified);

	compare_bits(verified, "11111111");
	TEST_CHECK(flatten(dst) == src);
}

TORRENT_TEST(merkle_validate_copy_full_odd_nodes)
{
	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,c,d,e,f,g,h};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	// we pretend that h is a padding node. This algorithm doesn't care that
	// it's not zero (yet)
	bitfield verified(7);

	merkle_validate_copy(src, dst, ah, verified);

	compare_bits(verified, "1111111");
	TEST_CHECK(flatten(dst) == src);
}


TORRENT_TEST(merkle_validate_copy_invalid_leaf)
{
	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,c,d,e,ef,g,h};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	bitfield verified(8);

	merkle_validate_copy(src, dst, ah, verified);

	// leaf 5 had an invalid hash, it's sibling (leaf 4) could also not be
	// validated because of it
	compare_bits(verified, "11110011");

	v const expected{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,c,d,o,o,g,h};
	TEST_CHECK(flatten(dst) == expected);
}

TORRENT_TEST(merkle_validate_copy_many_invalid_leafs)
{
	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,ef,d,eh,ef,g,ah};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	bitfield verified(8);

	merkle_validate_copy(src, dst, ah, verified);

	// leaf 2,4, 5 and 7 had an invalid hash, their siblings (leaf 3 and 6) could also not be
	// validated because of it
	compare_bits(verified, "11000000");

	v const expected{
	       ah,
	   ad,     eh,
	 ab, cd, ef, gh,
	a,b,o,o,o,o,o,o};
	TEST_CHECK(flatten(dst) == expected);
}

TORRENT_TEST(merkle_validate_copy_partial)
{
	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, o,
	a,b,c,o,o,o,o,o};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	bitfield verified(8);

	merkle_validate_copy(src, dst, ah, verified);

	compare_bits(verified, "11000000");

	v const expected{
	       ah,
	   ad,     eh,
	 ab, cd,  o, o,
	a,b,o,o,o,o,o,o};

	TEST_CHECK(flatten(dst) == expected);
}

TORRENT_TEST(merkle_validate_copy_invalid_root)
{
	v const src{
	       ah,
	   ad,     eh,
	 ab, cd, ef, o,
	a,b,c,o,o,o,o,o};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	bitfield verified(8);

	merkle_validate_copy(src, dst, a, verified);

	v const expected(15);

	compare_bits(verified, "00000000");
	TEST_CHECK(flatten(dst) == expected);
}

TORRENT_TEST(merkle_validate_copy_root_only)
{
	v const src{
	       ah,
	    o,      o,
	  o,  o,  o, o,
	o,o,o,o,o,o,o,o};

	sha256_hash const zero{};
	auto dst = make_empty_dst(8, zero);
	bitfield verified(8);

	merkle_validate_copy(src, dst, ah, verified);

	compare_bits(verified, "00000000");

	v const expected{
	       ah,
	    o,      o,
	  o,  o,  o, o,
	o,o,o,o,o,o,o,o};

	TEST_CHECK(flatten(dst) == expected);
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
	auto t = make_tree(v{ah, ad, eh, ab, cd, ef, gh, a, b, c, d, e, f, g, h});

	// root_index=3 -> (root_L=2, root_O=0)
	TEST_CHECK(merkle_find_known_subtree(t, 1, 8) == std::make_tuple(0, 2, 2, 0));
}

TORRENT_TEST(is_subtree_known_two_levels)
{
	auto t = make_tree(v{ah, ad, eh, o, o, ef, gh, a, b, c, d, e, f, g, h});

	// root_index=1 -> (root_L=1, root_O=0)
	TEST_CHECK(merkle_find_known_subtree(t, 1, 8) == std::make_tuple(0, 4, 1, 0));
}

TORRENT_TEST(is_subtree_known_unknown)
{
	auto t = make_tree(v{ah, ad, eh, o, o, ef, gh, a, b, o, d, e, f, g, h});

	// root_index=3 -> (root_L=2, root_O=0)
	TEST_CHECK(merkle_find_known_subtree(t, 1, 8) == std::make_tuple(0, 2, 2, 0));
}

TORRENT_TEST(is_subtree_known_padding)
{
	// the last leaf is padding, it should be assumed to be correct despite
	// being zero
	auto t = make_tree(v{ah, ad, eh, o, o, ef, gh, a, b, o, d, e, f, g, o});

	// root_index=6 -> (root_L=2, root_O=3)
	TEST_CHECK(merkle_find_known_subtree(t, 6, 7) == std::make_tuple(6, 2, 2, 3));
}

TORRENT_TEST(is_subtree_known_padding_two_levels)
{
	// the last leaf is padding, it should be assumed to be correct despite
	// being zero
	auto t = make_tree(v{ah, ad, eh, o, o, o, o, a, b, o, d, e, f, g, o});

	// root_index=2 -> (root_L=1, root_O=1)
	TEST_CHECK(merkle_find_known_subtree(t, 6, 7) == std::make_tuple(4, 4, 1, 1));
}

TORRENT_TEST(is_subtree_known_more_padding_two_levels)
{
	// the last two leafs are padding, they should be assumed to be correct despite
	// being zero
	auto t = make_tree(v{ah, ad, eh, o, o, o, o, a, b, o, d, e, f, o, o});

	// root_index=2 -> (root_L=1, root_O=1)
	TEST_CHECK(merkle_find_known_subtree(t, 5, 6) == std::make_tuple(4, 4, 1, 1));
}

namespace {
	// Build a fresh tree with only `tree[0] = root_hash` set, in full_tree
	// mode, for the proof-validation tests below.
	aux::merkle_tree make_root_only(sha256_hash const& root_hash)
	{
		aux::merkle_tree t(8, 1, root_hash.data());
		t.allocate_compact();
		return t;
	}
}

TORRENT_TEST(validate_and_insert_proofs_mixed)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs{f, gh, ad};

// target node_idx 11 -> (L=3, O=4)
TEST_CHECK(merkle_validate_and_insert_proofs(t, 3, 4, e, proofs));
TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, e, f, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_mixed_failure)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(eh); // this is not the correct root

v const proofs{f, gh, ad};

// target node_idx 11 -> (L=3, O=4)
TEST_CHECK(!merkle_validate_and_insert_proofs(t, 3, 4, e, proofs));

// make sure all nodes that were filled in were cleared correctly
TEST_CHECK((flatten(t) == v{eh, o, o, o, o, o, o, o, o, o, o, o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_left)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs{b, cd, eh};

// target node_idx 7 -> (L=3, O=0)
TEST_CHECK(merkle_validate_and_insert_proofs(t, 3, 0, a, proofs));
TEST_CHECK((flatten(t) == v{ah, ad, eh, ab, cd, o, o, a, b, o, o, o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_right)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs{g, ef, ad};

// target node_idx 14 -> (L=3, O=7)
TEST_CHECK(merkle_validate_and_insert_proofs(t, 3, 7, h, proofs));
TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, o, o, g, h}));
}

TORRENT_TEST(validate_and_insert_proofs_early_success)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);
t.set(1, 0, ad); // tree[1] = ad
t.set(1, 1, eh); // tree[2] = eh

v const proofs{f, gh, ad};

// target node_idx 11 -> (L=3, O=4)
TEST_CHECK(merkle_validate_and_insert_proofs(t, 3, 4, e, proofs));
TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, e, f, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_early_failure)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);
t.set(1, 0, ad); // tree[1] = ad
t.set(1, 1, ah); // tree[2] = ah <- this is not the right hash; validation should fail

v const proofs{f, gh, ad};

// target node_idx 11 -> (L=3, O=4)
TEST_CHECK(!merkle_validate_and_insert_proofs(t, 3, 4, e, proofs));

// make sure tree was correctly restored
TEST_CHECK((flatten(t) == v{ah, ad, ah, o, o, o, o, o, o, o, o, o, o, o, o}));
}


TORRENT_TEST(validate_and_insert_proofs_no_uncles)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs;

// target node_idx 1 -> (L=1, O=0)
TEST_CHECK(!merkle_validate_and_insert_proofs(t, 1, 0, ad, proofs));

// make sure tree was correctly restored
TEST_CHECK((flatten(t) == v{ah, o, o, o, o, o, o, o, o, o, o, o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_root)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs;

// this is just attempting to prove the root, which is ok.
// target node_idx 0 -> (L=0, O=0)
TEST_CHECK(merkle_validate_and_insert_proofs(t, 0, 0, ah, proofs));

// nothing happens to the tree in this case, we already had the root
TEST_CHECK((flatten(t) == v{ah, o, o, o, o, o, o, o, o, o, o, o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_root_fail)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs;

// this is just attempting to prove the root, but with the wrong hash.
// target node_idx 0 -> (L=0, O=0)
TEST_CHECK(!merkle_validate_and_insert_proofs(t, 0, 0, a, proofs));

// nothing happens to the tree in this case
TEST_CHECK((flatten(t) == v{ah, o, o, o, o, o, o, o, o, o, o, o, o, o, o}));
}

TORRENT_TEST(validate_and_insert_proofs_too_many_uncles)
{
// full tree:
//       ah
//    ad      eh
//  ab  cd  ef  gh
// a b c d  e f g h

auto t = make_root_only(ah);

v const proofs{f, gh, ad, a, b, c, d};

// target node_idx 11 -> (L=3, O=4)
TEST_CHECK(merkle_validate_and_insert_proofs(t, 3, 4, e, proofs));
TEST_CHECK((flatten(t) == v{ah, ad, eh, o, o, ef, gh, o, o, o, o, e, f, o, o}));
}

// A pre-existing sibling slot that already holds the same value the proof
// would write into it must not cause the call to fail or to disturb that
// slot. This models the merkle_tree::set_block path (a leaf-layer block
// hash speculatively stored before its sibling's proof arrives).
TORRENT_TEST(validate_and_insert_proofs_existing_sibling_match)
{
	// full tree:
	//       ah
	//    ad      eh
	//  ab  cd  ef  gh
	// a b c d  e f g h

	v tree(15);
	tree[0] = ah;
	tree[12] = f; // pre-existing matching sibling at the leaf layer

	v const proofs{f, gh, ad};

	TEST_CHECK(merkle_validate_and_insert_proofs(tree, 11, e, proofs));
	// clang-format off
	TEST_CHECK((tree == v{
	          ah,
	     ad,       eh,
	  o,    o,  ef,   gh,
	o, o, o, o,e, f, o, o}));
	// clang-format on
}

// A pre-existing sibling holding a value that contradicts the proof must
// cause a clean failure with the tree left untouched. (The class invariant
// for interior nodes guarantees existing non-zero values are correct, so
// the incoming proof is by definition wrong; for the leaf layer it lets
// us detect a bad proof against a previously-stored speculative hash
// without losing that hash.)
TORRENT_TEST(validate_and_insert_proofs_existing_sibling_mismatch)
{
	// full tree:
	//       ah
	//    ad      eh
	//  ab  cd  ef  gh
	// a b c d  e f g h

	v tree(15);
	tree[0] = ah;
	tree[12] = g; // contradicts the proof (which is f)

	v const proofs{f, gh, ad};

	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 11, e, proofs));

	// tree state must be fully preserved
	// clang-format off
	TEST_CHECK((tree == v{
	          ah,
	      o,        o,
	  o,    o,   o,    o,
	o, o, o, o,o, g, o, o}));
	// clang-format on
}

// Failure on a mid-walk mismatched known parent must not clear a sibling
// slot whose value was pre-existing rather than written by this call.
// Same scenario as the previous test, but with the mismatch one level
// up so the walk sees the matching sibling first and then fails.
TORRENT_TEST(validate_and_insert_proofs_existing_sibling_walk_failure)
{
	// full tree:
	//       ah
	//    ad      eh
	//  ab  cd  ef  gh
	// a b c d  e f g h

	v tree(15);
	tree[0] = ah;
	tree[2] = ad; // wrong "eh" at layer 1, will fail at the parent check
	tree[12] = f; // pre-existing matching sibling at the leaf layer

	v const proofs{f, gh, ad};

	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 11, e, proofs));

	// the pre-existing leaf sibling must survive the failure
	// clang-format off
	TEST_CHECK((tree == v{
	          ah,
	      o,       ad,
	  o,    o,   o,    o,
	o, o, o, o,o, f, o, o}));
	// clang-format on
}

// Companion to the test above, but with the walk exhausting `uncle_hashes`
// before reaching a known anchor (rather than breaking on a mismatched
// parent). The cleanup path is the same shape, and the pre-existing
// matching sibling must survive there too.
TORRENT_TEST(validate_and_insert_proofs_existing_sibling_short_uncles)
{
	// full tree:
	//       ah
	//    ad      eh
	//  ab  cd  ef  gh
	// a b c d  e f g h

	v tree(15);
	tree[0] = ah;
	tree[12] = f; // pre-existing matching sibling at the leaf layer

	// only one uncle: the walk computes one parent and then runs out
	// without ever reaching a node the tree already trusts
	v const proofs{f};

	TEST_CHECK(!merkle_validate_and_insert_proofs(tree, 11, e, proofs));

	// clang-format off
	TEST_CHECK((tree == v{
	          ah,
	      o,        o,
	  o,    o,   o,    o,
	o, o, o, o,o, f, o, o}));
	// clang-format on
}
