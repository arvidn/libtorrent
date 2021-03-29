/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019-2020, Steven Siloti
Copyright (c) 2020-2021, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>

#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"

#include "test.hpp"
#include "test_utils.hpp"

using namespace lt;

namespace {

int const num_blocks = 260;
auto const f = build_tree(num_blocks);
int const num_leafs = merkle_num_leafs(num_blocks);
int const num_nodes = merkle_num_nodes(num_leafs);
int const num_pad_leafs = num_leafs - num_blocks;

}

TORRENT_TEST(load_tree)
{
	// test with full tree and valid root
	{
		aux::merkle_tree t(260, 1, f[0].data());
		t.load_tree(f);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
	}

	// mismatching root hash
	{
		sha256_hash const bad_root("01234567890123456789012345678901");
		aux::merkle_tree t(260, 1, bad_root.data());
		t.load_tree(f);
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}

	// mismatching size
	{
		aux::merkle_tree t(260, 1, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(f.end_index() - 1));
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}
}

TORRENT_TEST(load_sparse_tree)
{
	// test with full tree and valid root
	{
		std::vector<bool> mask(f.size(), true);
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		t.load_sparse_tree(f, mask);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
	}

	// mismatching root hash
	{
		sha256_hash const bad_root("01234567890123456789012345678901");
		aux::merkle_tree t(num_blocks, 1, bad_root.data());
		std::vector<bool> mask(f.size(), false);
		mask[1] = true;
		mask[2] = true;
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(1, 2), mask);
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}

	// block layer
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		int const first_block = merkle_first_leaf(num_leafs);
		int const end_block = first_block + num_blocks;
		std::vector<bool> mask(f.size(), false);
		for (int i = first_block; i < end_block; ++i)
			mask[std::size_t(i)] = true;
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_block, num_blocks), mask);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
	}

	// piece layer
	{
		int const num_pieces = (num_blocks + 1) / 2;
		int const first_piece = merkle_first_leaf(merkle_num_leafs(num_pieces));
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		std::vector<bool> mask(f.size(), false);
		for (int i = first_piece, end = i + num_pieces; i < end; ++i)
			mask[std::size_t(i)] = true;
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_piece, num_pieces), mask);
		int const end_piece_layer = first_piece + merkle_num_leafs(num_pieces);
		for (int i = 0; i < end_piece_layer; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = end_piece_layer; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
		}
	}
}

namespace {
void test_roundtrip(aux::merkle_tree const& t
	, int const block_count
	, int const blocks_per_piece)
{
	auto const [tree, mask] = t.build_sparse_vector();

	aux::merkle_tree t2(block_count, blocks_per_piece, f[0].data());
	t2.load_sparse_tree(tree, mask);

	TEST_CHECK(t.build_vector() == t2.build_vector());
}
}

TORRENT_TEST(roundtrip_merkle_tree)
{
	// empty tree
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		test_roundtrip(t, num_blocks, 1);
	}

	// full tree
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		t.load_tree(f);
		test_roundtrip(t, num_blocks, 1);
	}

	// piece layer tree
	{
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		auto sparse_tree = f;
		for (int i = f.end_index() / 2; i < f.end_index(); ++i)
			sparse_tree[i] = lt::sha256_hash{};
		t.load_tree(sparse_tree);
		test_roundtrip(t, num_blocks, 2);
	}

	// some hashes tree
	{
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		auto sparse_tree = f;
		for (int i = f.end_index() / 4; i < f.end_index(); ++i)
		{
			if ((i % 3) == 0)
				sparse_tree[i] = lt::sha256_hash{};
		}

		t.load_tree(sparse_tree);
		test_roundtrip(t, num_blocks, 2);
	}

	// some more hashes tree
	{
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		auto sparse_tree = f;
		for (int i = f.end_index() / 4; i < f.end_index(); ++i)
		{
			if ((i % 4) == 0)
				sparse_tree[i] = lt::sha256_hash{};
		}

		t.load_tree(sparse_tree);
		test_roundtrip(t, num_blocks, 2);
	}

	// 1 block tree
	{
		aux::merkle_tree t(1, 256, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(1));
		test_roundtrip(t, 1, 256);
	}

	// 2 block tree
	{
		aux::merkle_tree t(2, 256, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(3));
		test_roundtrip(t, 2, 256);
	}

	// 2 block, partial tree
	{
		auto pf = f;
		pf.resize(3);
		pf[2].clear();
		aux::merkle_tree t(2, 256, f[0].data());
		t.load_tree(pf);
		test_roundtrip(t, 2, 256);
	}
}

TORRENT_TEST(small_tree)
{
	// a tree with a single block but large piece size
	aux::merkle_tree t(1, 256, f[0].data());

	TEST_CHECK(t.build_vector() == std::vector<lt::sha256_hash>{f[0]});
}

// the top 4 layers of the tree:
//                        0
//             1                     2
//       3          4            5         6
//   7     8     9    10    11    12     13   14
// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30

TORRENT_TEST(add_proofs_left_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[19], f[20]},
		{f[9], f[10]},
		{f[3], f[4]},
		{f[1], f[2]}
	};

	t.add_proofs(19, proofs);

	TEST_CHECK(t[19] == f[19]);
	TEST_CHECK(t[20] == f[20]);
	TEST_CHECK(t[9]  == f[9]);
	TEST_CHECK(t[10] == f[10]);
	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[4]  == f[4]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_proofs_right_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[19], f[20]},
		{f[9], f[10]},
		{f[3], f[4]},
		{f[1], f[2]}
	};

	// the only difference is that we say index 20 is the start index,
	// everything else is the same
	t.add_proofs(20, proofs);

	TEST_CHECK(t[19] == f[19]);
	TEST_CHECK(t[20] == f[20]);
	TEST_CHECK(t[9]  == f[9]);
	TEST_CHECK(t[10] == f[10]);
	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[4]  == f[4]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_proofs_far_left_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[15], f[16]},
		{f[7], f[8]},
		{f[3], f[4]},
		{f[1], f[2]}
	};

	t.add_proofs(15, proofs);

	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[4]  == f[4]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_proofs_far_right_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[29], f[30]},
		{f[13], f[14]},
		{f[5], f[6]},
		{f[1], f[2]}
	};

	t.add_proofs(29, proofs);

	TEST_CHECK(t[29] == f[29]);
	TEST_CHECK(t[30] == f[30]);
	TEST_CHECK(t[13] == f[13]);
	TEST_CHECK(t[14] == f[14]);
	TEST_CHECK(t[5]  == f[5]);
	TEST_CHECK(t[6]  == f[6]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_hashes_pass)
{
	std::vector<sha256_hash> const subtree{
		f[3],
		f[7], f[8],
		f[15], f[16], f[17], f[18],
	};

	aux::merkle_tree t(260, 1, f[0].data());
	auto const failed = t.add_hashes(15, subtree);
	TEST_CHECK(failed.empty());

	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[17] == f[17]);
	TEST_CHECK(t[18] == f[18]);
}

using p = std::map<piece_index_t, std::vector<int>>;

// this is the full tree:
//                        0
//             1                     2
//       3          4            5         6
//   7     8     9    10    11    12     13   14
// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30
TORRENT_TEST(add_hashes_fail1)
{
	std::vector<sha256_hash> const subtree{
		f[3],
		f[7], f[8],
		f[15], f[16], f[17], f[18],
	};

	aux::merkle_tree t(13, 1, f[0].data());

	// this is an invalid hash
	t.set_block(1, sha256_hash("01234567890123456789012345678901"));

	auto const failed = t.add_hashes(15, subtree);
	TEST_CHECK((failed == p{{1_piece, {0}}}));

	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[17] == f[17]);
	TEST_CHECK(t[18] == f[18]);
}

TORRENT_TEST(add_hashes_fail2)
{
	std::vector<sha256_hash> const subtree{
		f[3],
		f[7], f[8],
		f[15], f[16], f[17], f[18],
	};

	aux::merkle_tree t(13, 2, f[0].data());

	// this is an invalid hash
	t.set_block(1, sha256_hash("01234567890123456789012345678901"));
	t.set_block(2, sha256_hash("01234567890123456789012345678901"));
	t.set_block(3, sha256_hash("01234567890123456789012345678901"));

	auto const failed = t.add_hashes(15, subtree);
	TEST_CHECK((failed == p{{0_piece, {1}}, {1_piece, {0, 1}}}));

	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[17] == f[17]);
	TEST_CHECK(t[18] == f[18]);
}

// the 4 layers of the tree:
//                        0
//             1                     2
//       3          4            5         6
//   7     8     9    10    11    12     13   14
// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30

TORRENT_TEST(sparse_merkle_tree_block_layer)
{
	aux::merkle_tree t(260, 2, f[0].data());

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())));

	for (int i = 0; i < int(t.size()); ++i)
	{
		std::cout << i << '\n';
		TEST_CHECK(t[i] == f[i]);
	}
}

TORRENT_TEST(get_piece_layer)
{
	// 8 blocks per piece.
	aux::merkle_tree t(260, 8, f[0].data());
	t.load_tree(span<sha256_hash const>(f).first(int(t.size())));

	int const num_pieces = (260 + 7) / 8;
	int const piece_layer_size = merkle_num_leafs(num_pieces);
	int const piece_layer_start = merkle_first_leaf(piece_layer_size);
	auto const piece_layer = t.get_piece_layer();

	TEST_EQUAL(num_pieces, int(piece_layer.size()));
	for (int i = 0; i < int(piece_layer.size()); ++i)
	{
		TEST_CHECK(t[piece_layer_start + i] == piece_layer[i]);
	}
}

// TODO: add test for load_piece_layer()
// TODO: add test for get_piece_layer() when tree is in piece-layer mode

namespace {
using s = span<sha256_hash const>;

span<sha256_hash const> range(std::vector<sha256_hash> const& c, int first, int count)
{
	return s(c).subspan(first, count);
}
}

TORRENT_TEST(merkle_tree_get_hashes)
{
	aux::merkle_tree t(260, 2, f[0].data());

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())));

	// all nodes leaf layer
	{
		auto h = t.get_hashes(0, 0, 260, 0);
		TEST_CHECK(s(h) == range(f, 511, 260));
	}

	// all nodes leaf layer but the first
	{
		auto h = t.get_hashes(0, 1, 259, 0);
		TEST_CHECK(s(h) == range(f, 512, 259));
	}

	// all nodes leaf layer but the last
	{
		auto h = t.get_hashes(0, 0, 259, 0);
		TEST_CHECK(s(h) == range(f, 511, 259));
	}

	// one layer up
	{
		auto h = t.get_hashes(1, 0, 256, 0);
		TEST_CHECK(s(h) == range(f, 255, 256));
	}

	// one layer up + one layer proof
	{
		auto h = t.get_hashes(1, 0, 4, 2);
		TEST_CHECK(s(h).first(4) == range(f, 255, 4));

		// the proof is the sibling to the root of the tree we got back.
		// the hashes are rooted at 255 / 2 / 2 = 63
		std::vector<sha256_hash> const proofs{f[merkle_get_sibling(63)]};
		TEST_CHECK(s(h).subspan(4) == s(proofs));
	}

	// one layer up, hashes 2 - 10, 5 proof layers
	{
		auto h = t.get_hashes(1, 2, 8, 5);
		TEST_CHECK(s(h).first(8) == range(f, 255 + 2, 8));

		// the proof is the sibling to the root of the tree we got back.
		int const start_proofs = merkle_get_parent(merkle_get_parent(merkle_get_parent(257)));
		std::vector<sha256_hash> const proofs{
			f[merkle_get_sibling(start_proofs)]
			, f[merkle_get_sibling(merkle_get_parent(start_proofs))]
			, f[merkle_get_sibling(merkle_get_parent(merkle_get_parent(start_proofs)))]
			};
		TEST_CHECK(s(h).subspan(8) == s(proofs));
	}

	// full tree
	{
		auto h = t.get_hashes(0, 0, 512, 8);
		TEST_CHECK(s(h) == range(f, 511, 512));
		// there won't be any proofs, since we got the full tree
	}

	// second half of the tree
	{
		auto h = t.get_hashes(0, 256, 256, 8);
		TEST_CHECK(s(h).first(256) == range(f, 511 + 256, 256));

		// there just one proof hash
		std::vector<sha256_hash> const proofs{ f[1] };
		TEST_CHECK(s(h).subspan(256) == s(proofs));
	}

	// 3rd quarter of the tree
	{
		auto h = t.get_hashes(0, 256, 128, 8);
		TEST_CHECK(s(h).first(128) == range(f, 511 + 256, 128));

		// there just two proof hashes
		std::vector<sha256_hash> const proofs{ f[6], f[1] };
		TEST_CHECK(s(h).subspan(128) == s(proofs));
	}

	// 3rd quarter of the tree, starting one layer up
	{
		auto h = t.get_hashes(1, 128, 64, 7);
		TEST_CHECK(s(h).first(64) == range(f, 255 + 128, 64));

		// still just two proof hashes
		std::vector<sha256_hash> const proofs{ f[6], f[1] };
		TEST_CHECK(s(h).subspan(64) == s(proofs));
	}

	// 3rd quarter of the tree, starting one layer up
	// request no proof hashes
	{
		auto h = t.get_hashes(1, 128, 64, 0);
		TEST_CHECK(s(h) == range(f, 255 + 128, 64));
	}
}

