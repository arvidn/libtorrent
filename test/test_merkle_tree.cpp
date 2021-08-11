/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019-2020, Steven Siloti
Copyright (c) 2020, Arvid Norberg
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

using verified_t = std::vector<bool>;
verified_t const empty_verified(std::size_t(num_leafs), false);
}

TORRENT_TEST(load_tree)
{
	// test with full tree and valid root
	{
		aux::merkle_tree t(260, 1, f[0].data());
		t.load_tree(f, empty_verified);
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
		t.load_tree(f, empty_verified);
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}

	// mismatching size
	{
		aux::merkle_tree t(260, 1, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(f.end_index() - 1), empty_verified);
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
		t.load_sparse_tree(f, mask, empty_verified);
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
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(1, 2), mask, empty_verified);
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
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_block, num_blocks), mask, empty_verified);
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
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_piece, num_pieces), mask, empty_verified);
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
	// TODO: use structured bindings in C++17
	aux::vector<bool> mask;
	std::vector<sha256_hash> tree;
	std::tie(tree, mask) = t.build_sparse_vector();

	aux::merkle_tree t2(block_count, blocks_per_piece, f[0].data());
	t2.load_sparse_tree(tree, mask, empty_verified);

	TEST_CHECK(t.build_vector() == t2.build_vector());
	for (int i = 0; i < int(t.size()); ++i)
	{
		TEST_EQUAL(t[i], t2[i]);
		TEST_EQUAL(t.has_node(i), t2.has_node(i));

		if (!t.has_node(i))
			TEST_CHECK(t[i].is_all_zeros());
		if (!t2.has_node(i))
			TEST_CHECK(t2[i].is_all_zeros());

		TEST_CHECK(t.compare_node(i, t2[i]));
		TEST_CHECK(t2.compare_node(i, t[i]));
	}
}
}

TORRENT_TEST(roundtrip_empty_tree)
{
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	test_roundtrip(t, num_blocks, 1);
}

TORRENT_TEST(roundtrip_full_tree)
{
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	t.load_tree(f, empty_verified);
	test_roundtrip(t, num_blocks, 1);
}

TORRENT_TEST(roundtrip_piece_layer_tree)
{
	aux::merkle_tree t(num_blocks, 2, f[0].data());
	auto sparse_tree = f;
	for (int i = f.end_index() / 2; i < f.end_index(); ++i)
		sparse_tree[i] = lt::sha256_hash{};
	t.load_tree(sparse_tree, empty_verified);
	test_roundtrip(t, num_blocks, 2);
}

TORRENT_TEST(roundtrip_partial_tree)
{
	aux::merkle_tree t(num_blocks, 2, f[0].data());
	auto sparse_tree = f;
	for (int i = f.end_index() / 4; i < f.end_index(); ++i)
	{
		if ((i % 3) == 0)
			sparse_tree[i] = lt::sha256_hash{};
	}

	t.load_tree(sparse_tree, empty_verified);
	test_roundtrip(t, num_blocks, 2);
}

TORRENT_TEST(roundtrip_more_partial_tree)
{
	aux::merkle_tree t(num_blocks, 2, f[0].data());
	auto sparse_tree = f;
	for (int i = f.end_index() / 4; i < f.end_index(); ++i)
	{
		if ((i % 4) == 0)
			sparse_tree[i] = lt::sha256_hash{};
	}

	t.load_tree(sparse_tree, empty_verified);
	test_roundtrip(t, num_blocks, 2);
}

TORRENT_TEST(roundtrip_one_block_tree)
{
	aux::merkle_tree t(1, 256, f[0].data());
	t.load_tree(span<sha256_hash const>(f).first(1), empty_verified);
	test_roundtrip(t, 1, 256);
}

TORRENT_TEST(roundtrip_two_block_tree)
{
	aux::merkle_tree t(2, 256, f[0].data());
	t.load_tree(span<sha256_hash const>(f).first(3), empty_verified);
	test_roundtrip(t, 2, 256);
}

TORRENT_TEST(roundtrip_two_block_partial_tree)
{
	auto pf = f;
	pf.resize(3);
	pf[2].clear();
	aux::merkle_tree t(2, 256, f[0].data());
	t.load_tree(pf, empty_verified);
	test_roundtrip(t, 2, 256);
}

TORRENT_TEST(small_tree)
{
	// a tree with a single block but large piece size
	aux::merkle_tree t(1, 256, f[0].data());

	TEST_CHECK(t.build_vector() == std::vector<lt::sha256_hash>{f[0]});
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

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())), empty_verified);

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
	t.load_tree(span<sha256_hash const>(f).first(int(t.size())), empty_verified);

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
// TODO: add test for add_hashes()
// TODO: add test for operator[] in each mode the tree can be in. piece-layer,
// block-layer, full and empty

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

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())), empty_verified);

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

