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
#include "libtorrent/random.hpp"

#include "test.hpp"
#include "test_utils.hpp"

using namespace lt;

namespace {

int const num_blocks = 259;
auto const f = build_tree(num_blocks);
int const num_leafs = merkle_num_leafs(num_blocks);
int const num_nodes = merkle_num_nodes(num_leafs);
int const num_pad_leafs = num_leafs - num_blocks;

using verified_t = std::vector<bool>;
verified_t const empty_verified(std::size_t(num_blocks), false);

using s = span<sha256_hash const>;

span<sha256_hash const> range(std::vector<sha256_hash> const& c, int first, int count)
{
	return s(c).subspan(first, count);
}

sha256_hash rand_sha256()
{
	sha256_hash ret;
	aux::random_bytes(ret);
	return ret;
}

std::vector<sha256_hash> build_proof(span<sha256_hash const> tree, int target, int end = 0)
{
	std::vector<sha256_hash> ret;
	while (target > end)
	{
		ret.push_back(tree[merkle_get_sibling(target)]);
		target = merkle_get_parent(target);
	}
	return ret;
}

std::vector<sha256_hash> corrupt(span<sha256_hash const> hashes)
{
	std::vector<sha256_hash> ret;
	ret.reserve(std::size_t(hashes.size()));
	std::copy(hashes.begin(), hashes.end(), std::back_inserter(ret));
	ret[146542934 % ret.size()][2] ^= 0x26;
	return ret;
}

std::vector<bool> all_set(int count)
{
	return std::vector<bool>(std::size_t(count), true);
}

std::vector<bool> none_set(int count)
{
	return std::vector<bool>(std::size_t(count), false);
}

std::vector<bool> set_range(std::vector<bool> bits, int start, int count)
{
	while (count > 0)
	{
		TORRENT_ASSERT(start >= 0);
		TORRENT_ASSERT(std::size_t(start) < bits.size());
		bits[std::size_t(start)] = true;
		++start;
		--count;
	}
	return bits;
}
}

TORRENT_TEST(load_tree)
{
	// test with full tree and valid root
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
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
		aux::merkle_tree t(num_blocks, 1, bad_root.data());
		t.load_tree(f, empty_verified);
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}

	// mismatching size
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
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
	t.load_tree(span<sha256_hash const>(f).first(3), verified_t(std::size_t(2), false));
	test_roundtrip(t, 2, 256);
}

TORRENT_TEST(roundtrip_two_block_partial_tree)
{
	auto pf = f;
	pf.resize(3);
	pf[2].clear();
	aux::merkle_tree t(2, 256, f[0].data());
	t.load_tree(pf, verified_t(std::size_t(2), false));
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
	aux::merkle_tree t(num_blocks, 2, f[0].data());

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())), empty_verified);

	for (int i = 0; i < int(t.size()); ++i)
		TEST_CHECK(t[i] == f[i]);
}

TORRENT_TEST(get_piece_layer)
{
	// 8 blocks per piece.
	aux::merkle_tree t(num_blocks, 8, f[0].data());
	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
	t.load_tree(span<sha256_hash const>(f).first(int(t.size())), empty_verified);

	int const num_pieces = (num_blocks + 7) / 8;
	int const piece_layer_size = merkle_num_leafs(num_pieces);
	int const piece_layer_start = merkle_first_leaf(piece_layer_size);
	auto const piece_layer = t.get_piece_layer();

	TEST_CHECK(t.verified_leafs() == all_set(num_blocks));

	TEST_EQUAL(num_pieces, int(piece_layer.size()));
	for (int i = 0; i < int(piece_layer.size()); ++i)
	{
		TEST_CHECK(t[piece_layer_start + i] == piece_layer[i]);
	}
}

TORRENT_TEST(get_piece_layer_piece_layer_mode)
{
	aux::merkle_tree t(num_blocks, 4, f[0].data());
	int const num_pieces = (num_blocks + 3) / 4;

	// add the entire piece layer
	t.load_piece_layer(span<char const>(f[127].data(), sha256_hash::size() * num_pieces));

	int const piece_layer_size = merkle_num_leafs(num_pieces);
	int const piece_layer_start = merkle_first_leaf(piece_layer_size);
	auto const piece_layer = t.get_piece_layer();

	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));

	TEST_EQUAL(num_pieces, int(piece_layer.size()));
	for (int i = 0; i < int(piece_layer.size()); ++i)
	{
		TEST_CHECK(t[piece_layer_start + i] == piece_layer[i]);
	}
}

TORRENT_TEST(merkle_tree_get_hashes)
{
	aux::merkle_tree t(num_blocks, 2, f[0].data());

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())), empty_verified);

	// all nodes leaf layer
	{
		auto h = t.get_hashes(0, 0, num_blocks, 0);
		TEST_CHECK(s(h) == range(f, 511, num_blocks));
	}

	// all nodes leaf layer but the first
	{
		auto h = t.get_hashes(0, 1, num_blocks - 1, 0);
		TEST_CHECK(s(h) == range(f, 512, num_blocks - 1));
	}

	// all nodes leaf layer but the last
	{
		auto h = t.get_hashes(0, 0, num_blocks - 1, 0);
		TEST_CHECK(s(h) == range(f, 511, num_blocks - 1));
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

//                             0
//                  1                     2
//            3          4            5         6
//        7     8     9    10    11    12     13   14
//      15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30
//     31      ...                                    62
//    63      ...                                      126
//   127     ...                                        254  <- piece layer
//  255     ...                                          510
// 511     ...         771   ... padding ...              1022 <- block layer

using pdiff = piece_index_t::diff_type;

TORRENT_TEST(add_hashes_full_tree)
{
	for (int blocks_per_piece : {1, 2, 4})
	{
		aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

		// add the entire block layer
		auto const result = t.add_hashes(511, pdiff(1), range(f, 511, 512), span<sha256_hash const>());

		TEST_CHECK(result);
		if (!result) return;

		auto const& res = *result;
		TEST_EQUAL(res.passed.size(), 0);
		TEST_EQUAL(res.failed.size(), 0);

		// check the piece layer
		for (int i = 127; i < 255; ++i)
			TEST_EQUAL(t[i], f[i]);

		// check the block layer
		for (int i = 511; i < 1023; ++i)
			TEST_EQUAL(t[i], f[i]);

		TEST_CHECK(t.verified_leafs() == all_set(num_blocks));
	}
}

TORRENT_TEST(add_hashes_one_piece)
{
	int const blocks_per_piece = 4;
	for (int piece_index : {0, 64, 5, 30})
	{
		aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

		int const insert_idx = 127 + piece_index;
		auto const result = t.add_hashes(511 + piece_index * blocks_per_piece, pdiff(1)
			, range(f, 511 + piece_index * blocks_per_piece, blocks_per_piece)
			, build_proof(f, insert_idx));

		TEST_CHECK(result);
		if (!result) return;

		auto const& res = *result;
		TEST_EQUAL(res.passed.size(), 0);
		TEST_EQUAL(res.failed.size(), 0);

		// the trail of proof hashes
		for (int i = insert_idx; i > 0; i = merkle_get_parent(i))
		{
			TEST_EQUAL(t[i], f[i]);
			TEST_EQUAL(t[merkle_get_sibling(i)], f[merkle_get_sibling(i)]);
		}

		// check the piece layer
		for (int i = 127; i < 255; ++i)
		{
			// one is the root of the hashes we added, the other is part of the
			// proof anchroing it in the root
			if (i == 127 + piece_index || merkle_get_sibling(i) == 127 + piece_index)
				TEST_EQUAL(t[i], f[i]);
			else
				TEST_CHECK(t[i].is_all_zeros());
		}

		// check the block layer
		for (int i = 511; i < 1023; ++i)
		{
			if (i >= 511 + piece_index*blocks_per_piece && i < 511 + piece_index*blocks_per_piece + 4)
				TEST_EQUAL(t[i], f[i]);
			else
				TEST_CHECK(t[i].is_all_zeros());
		}

		int const start_block = piece_index * blocks_per_piece;
		int const end_block = std::min(blocks_per_piece, num_blocks - start_block);
		TEST_CHECK(t.verified_leafs() == set_range(none_set(num_blocks)
			, start_block, end_block));
	}
}

TORRENT_TEST(add_hashes_one_piece_invalid_proof)
{
	int const blocks_per_piece = 4;
	for (int piece_index : {0, 64, 5, 30})
	{
		aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

		int const insert_idx = 127 + piece_index;
		auto const result = t.add_hashes(511 + piece_index * blocks_per_piece, pdiff(1)
			, range(f, 511 + piece_index * blocks_per_piece, blocks_per_piece)
			, corrupt(build_proof(f, insert_idx)));

		TEST_CHECK(!result);
		TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
	}
}

TORRENT_TEST(add_hashes_one_piece_invalid_hash)
{
	int const blocks_per_piece = 4;
	for (int piece_index : {0, 64, 5, 30})
	{
		aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

		int const insert_idx = 127 + piece_index;
		auto const result = t.add_hashes(511 + piece_index * blocks_per_piece, pdiff(1)
			, corrupt(range(f, 511 + piece_index * blocks_per_piece, blocks_per_piece))
			, build_proof(f, insert_idx));

		TEST_CHECK(!result);
		TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
	}
}

TORRENT_TEST(add_hashes_full_tree_existing_valid_blocks)
{
	for (int piece_index : {0, 63})
	{
		for (int blocks_per_piece : {1, 2, 4})
		{
			aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

			for (int i = 511 + piece_index * blocks_per_piece;
				i < 511 + std::min(piece_index * blocks_per_piece + 8, num_blocks);
				++i)
			{
				auto ret = t.set_block(i - 511, f[i]);
				TEST_CHECK(std::get<0>(ret) == aux::merkle_tree::set_block_result::unknown);
			}

			// add the entire block layer
			auto const result = t.add_hashes(511, pdiff(10), range(f, 511, 512), span<sha256_hash const>());

			TEST_CHECK(result);
			if (!result) return;

			auto const& res = *result;
			TEST_EQUAL(res.passed.size(), std::size_t(8 / blocks_per_piece));
			TEST_EQUAL(res.failed.size(), 0);

			piece_index_t idx(piece_index + 10);
			for (auto passed : res.passed)
			{
				TEST_EQUAL(passed, idx);
				++idx;
			}

			TEST_CHECK(t.verified_leafs() == all_set(num_blocks));
		}
	}
}

TORRENT_TEST(add_hashes_full_tree_existing_invalid_blocks)
{
	for (int piece_index : {0, 63})
	{
		std::cout << "piece: " << piece_index << std::endl;
		for (int blocks_per_piece : {1, 2, 4})
		{
			std::cout << "block per piece: " << blocks_per_piece << std::endl;
			aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

			for (int i = 511 + piece_index * blocks_per_piece;
				i < 511 + std::min(piece_index * blocks_per_piece + 8, num_blocks);
				++i)
			{
				// the hash is invalid
				auto ret = t.set_block(i - 511, rand_sha256());
				TEST_CHECK(std::get<0>(ret) == aux::merkle_tree::set_block_result::unknown);
			}

			// add the entire block layer
			auto const result = t.add_hashes(511, pdiff(10), range(f, 511, 512), span<sha256_hash const>());

			TEST_CHECK(result);
			if (!result) return;

			auto const& res = *result;
			TEST_EQUAL(res.passed.size(), 0);
			TEST_EQUAL(res.failed.size(), std::size_t(8 / blocks_per_piece));

			piece_index_t idx(piece_index);
			for (auto failed : res.failed)
			{
				TEST_EQUAL(failed.first, idx + pdiff(10));
				TEST_EQUAL(int(failed.second.size()), std::min(blocks_per_piece
					, num_blocks - static_cast<int>(idx) * blocks_per_piece));
				++idx;
				int block = 0;
				for (auto const b : failed.second)
				{
					TEST_EQUAL(b, block);
					++block;
				}
			}

			TEST_CHECK(t.verified_leafs() == all_set(num_blocks));
		}
	}
}

TORRENT_TEST(set_block_full_block_layer)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	{
		// add the entire block layer
		auto const result = t.add_hashes(511, pdiff(1), range(f, 511, 512), span<sha256_hash const>());
		TEST_CHECK(result);
		if (!result) return;
	}

	TEST_CHECK(t.verified_leafs() == all_set(num_blocks));

	for (int block = 0; block < num_blocks; ++block)
	{
		// the tree is complete, we know all hashes already. This is just
		// comparing the hash against what we have in the tree
		auto const result = t.set_block(block, f[511 + block]);
		TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::ok);
		TEST_EQUAL(std::get<1>(result), block);
		TEST_EQUAL(std::get<2>(result), 1);
	}
}

TORRENT_TEST(set_block_invalid_full_block_layer)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	{
		// add the entire block layer
		auto const result = t.add_hashes(511, pdiff(1), range(f, 511, 512), span<sha256_hash const>());
		TEST_CHECK(result);
		if (!result) return;
	}

	TEST_CHECK(t.verified_leafs() == all_set(num_blocks));

	for (int block = 0; block < num_blocks; ++block)
	{
		// the tree is complete, we know all hashes already. This is just
		// comparing the hash against what we have in the tree
		auto const result = t.set_block(block, rand_sha256());
		TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::block_hash_failed);
		TEST_EQUAL(std::get<1>(result), block);
		TEST_EQUAL(std::get<2>(result), 1);
	}
}

TORRENT_TEST(set_block_full_piece_layer)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	{
		// add the entire piece layer
		auto const result = t.add_hashes(127, pdiff(1), range(f, 127, 128), span<sha256_hash const>());
		TEST_CHECK(result);
		if (!result) return;
	}

	for (int block = 0; block < num_blocks; ++block)
	{
		auto const result = t.set_block(block, f[511 + block]);
		if ((block % blocks_per_piece) == blocks_per_piece - 1 || block == num_blocks - 1)
		{
			TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::ok);
			TEST_EQUAL(std::get<1>(result), block - (block % blocks_per_piece));
			TEST_EQUAL(std::get<2>(result), blocks_per_piece);
			TEST_CHECK(t.verified_leafs() == set_range(none_set(num_blocks), 0, block + 1));
		}
		else
		{
			TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::unknown);
			TEST_CHECK(t.verified_leafs() == set_range(none_set(num_blocks), 0, block - (block % blocks_per_piece)));
		}
	}
}

TORRENT_TEST(set_block_invalid_full_piece_layer)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	{
		// add the entire piece layer
		auto const result = t.add_hashes(127, pdiff(1), range(f, 127, 128), span<sha256_hash const>());
		TEST_CHECK(result);
		if (!result) return;
	}

	for (int block = 0; block < num_blocks; ++block)
	{
		auto const result = t.set_block(block, rand_sha256());
		if ((block % blocks_per_piece) == blocks_per_piece - 1 || block == num_blocks - 1)
		{
			TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::hash_failed);
			TEST_EQUAL(std::get<1>(result), block - (block % blocks_per_piece));
			TEST_EQUAL(std::get<2>(result), blocks_per_piece);
		}
		else
		{
			TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::unknown);
		}
		TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
	}
}

TORRENT_TEST(set_block_empty_tree)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	for (int block = 0; block < num_blocks - 1; ++block)
	{
		// the tree is complete, we know all hashes already. This is just
		// comparing the hash against what we have in the tree
		auto const result = t.set_block(block, f[511 + block]);
		TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::unknown);
		TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
	}

	int const block = num_blocks - 1;
	auto const result = t.set_block(block, f[511 + block]);

	TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::ok);
	TEST_EQUAL(std::get<1>(result), 0);
	TEST_EQUAL(std::get<2>(result), num_leafs);

	TEST_CHECK(t.verified_leafs() == all_set(num_blocks));
}

TORRENT_TEST(set_block_invalid_empty_tree)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	for (int block = 0; block < num_blocks; ++block)
	{
		// the tree is complete, we know all hashes already. This is just
		// comparing the hash against what we have in the tree
		auto const result = t.set_block(block, rand_sha256());
		TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::unknown);
		TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
	}
}

TORRENT_TEST(add_hashes_block_layer_no_padding)
{
	aux::merkle_tree t(num_blocks, 4, f[0].data());

	auto const result = t.add_hashes(511, pdiff(1), range(f, 511, num_blocks), span<sha256_hash const>());

	TEST_CHECK(result);
	if (!result) return;

	auto const& res = *result;
	TEST_EQUAL(res.passed.size(), 0);
	TEST_EQUAL(res.failed.size(), 0);

	for (int i = 0; i < 1023; ++i)
		TEST_EQUAL(t[i], f[i]);

	TEST_CHECK(t.verified_leafs() == all_set(num_blocks));
}

TORRENT_TEST(add_hashes_piece_layer_no_padding)
{
	aux::merkle_tree t(num_blocks, 4, f[0].data());

	int const num_pieces = (num_blocks + 3) / 4;
	auto const result = t.add_hashes(127, pdiff(1), range(f, 127, num_pieces), span<sha256_hash const>());

	TEST_CHECK(result);
	if (!result) return;

	auto const& res = *result;
	TEST_EQUAL(res.passed.size(), 0);
	TEST_EQUAL(res.failed.size(), 0);

	for (int i = 0; i < 255; ++i)
		TEST_EQUAL(t[i], f[i]);

	for (int i = 255; i < 1023; ++i)
		TEST_CHECK(t[i].is_all_zeros());

	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
}

TORRENT_TEST(add_hashes_partial_proofs)
{
	aux::merkle_tree t(num_blocks, 4, f[0].data());

	// set the first 2 layers
	{
		auto const result = t.add_hashes(3, pdiff(1), range(f, 3, 4), span<sha256_hash const>());
		TEST_CHECK(result);
		if (!result) return;

		for (int i = 0; i < 7; ++i)
			TEST_EQUAL(t[i], f[i]);
	}

	// use a proof that ties the first piece node 3 (since we don't need it all
	// the way to the root).
	auto const result = t.add_hashes(127, pdiff(1), range(f, 127, 4), build_proof(f, 31, 3));
	TEST_CHECK(result);

	auto const& res = *result;
	TEST_EQUAL(res.passed.size(), 0);
	TEST_EQUAL(res.failed.size(), 0);

	for (int i = 127; i < 127 + 4; ++i)
		TEST_CHECK(t[i] == f[i]);

	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
}

// TODO: add test for load_piece_layer()
// TODO: add test for add_hashes() with an odd number of blocks
// TODO: add test for set_block() (setting the last block) with an odd number of blocks

