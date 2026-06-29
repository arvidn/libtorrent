/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019-2020, Steven Siloti
Copyright (c) 2020, Alden Torres
Copyright (c) 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>

#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/bitfield.hpp"

#include "test.hpp"
#include "test_utils.hpp"

using namespace lt;

namespace {

int const num_blocks = 259;
auto const f = build_tree(num_blocks);
int const num_leafs = merkle_num_leafs(num_blocks);
int const num_nodes = merkle_num_nodes(num_leafs);
int const num_pad_leafs = num_leafs - num_blocks;

using verified_t = bitfield;
verified_t const empty_verified(num_blocks);

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

// Thin flat-index adapters over merkle_tree's (level, offset) API, used
// by tests that iterate the full standard-layout flat index space.
sha256_hash node_at_flat(aux::merkle_tree const& t, int const idx)
{
	int const L = merkle_get_layer(idx);
	return t.node_at(L, idx - merkle_layer_start(L));
}

bool has_node_at(aux::merkle_tree const& t, int const idx)
{
	int const L = merkle_get_layer(idx);
	return t.has_node(L, idx - merkle_layer_start(L));
}

bool compare_at(aux::merkle_tree const& t, int const idx, sha256_hash const& h)
{
	int const L = merkle_get_layer(idx);
	return t.compare_node(L, idx - merkle_layer_start(L), h);
}

bitfield all_set(int count)
{
	return bitfield(count, true);
}

bitfield none_set(int count)
{
	return bitfield(count);
}

bitfield set_range(bitfield bits, int start, int count)
{
	while (count > 0)
	{
		TORRENT_ASSERT(start >= 0);
		TORRENT_ASSERT(start < bits.size());
		bits.set_bit(start);
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
			TEST_CHECK(has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
	}

	// mismatching root hash
	{
		sha256_hash const bad_root("01234567890123456789012345678901");
		aux::merkle_tree t(num_blocks, 1, bad_root.data());
		t.load_tree(f, empty_verified);
		TEST_CHECK(has_node_at(t, 0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!has_node_at(t, i));
	}

	// mismatching size
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(f.end_index() - 1), empty_verified);
		TEST_CHECK(has_node_at(t, 0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!has_node_at(t, i));
	}
}

TORRENT_TEST(load_sparse_tree)
{
	// test with full tree and valid root
	{
		bitfield mask(int(f.size()), true);
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		t.load_sparse_tree(f, mask, empty_verified);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
	}

	// mismatching root hash
	{
		sha256_hash const bad_root("01234567890123456789012345678901");
		aux::merkle_tree t(num_blocks, 1, bad_root.data());
		bitfield mask(int(f.size()), false);
		mask.set_bit(1);
		mask.set_bit(2);
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(1, 2), mask, empty_verified);
		TEST_CHECK(has_node_at(t, 0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!has_node_at(t, i));
	}

	// block layer
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		int const first_block = merkle_first_leaf(num_leafs);
		int const end_block = first_block + num_blocks;
		bitfield mask(int(f.size()), false);
		for (int i = first_block; i < end_block; ++i)
			mask.set_bit(i);
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_block, num_blocks), mask, empty_verified);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
	}

	// piece layer
	{
		int const num_pieces = (num_blocks + 1) / 2;
		int const first_piece = merkle_first_leaf(merkle_num_leafs(num_pieces));
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		bitfield mask(int(f.size()), false);
		for (int i = first_piece, end = i + num_pieces; i < end; ++i)
			mask.set_bit(i);
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_piece, num_pieces), mask, empty_verified);
		int const end_piece_layer = first_piece + merkle_num_leafs(num_pieces);
		for (int i = 0; i < end_piece_layer; ++i)
		{
			TEST_CHECK(has_node_at(t, i));
			TEST_CHECK(compare_at(t, i, f[i]));
		}
		for (int i = end_piece_layer; i < num_nodes; ++i)
		{
			TEST_CHECK(!has_node_at(t, i));
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
	t2.load_sparse_tree(tree, mask, empty_verified);

	TEST_CHECK(t.build_vector() == t2.build_vector());
	for (int i = 0; i < int(t.size()); ++i)
	{
		TEST_EQUAL(node_at_flat(t, i), node_at_flat(t2, i));
		TEST_EQUAL(has_node_at(t, i), has_node_at(t2, i));

		if (!has_node_at(t, i)) TEST_CHECK(node_at_flat(t, i).is_all_zeros());
		if (!has_node_at(t2, i)) TEST_CHECK(node_at_flat(t2, i).is_all_zeros());

		TEST_CHECK(compare_at(t, i, node_at_flat(t2, i)));
		TEST_CHECK(compare_at(t2, i, node_at_flat(t, i)));
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
		TEST_CHECK(node_at_flat(t, i) == f[i]);
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
		TEST_CHECK(node_at_flat(t, piece_layer_start + i) == piece_layer[i]);
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
		TEST_CHECK(node_at_flat(t, piece_layer_start + i) == piece_layer[i]);
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
			TEST_EQUAL(node_at_flat(t, i), f[i]);

		// check the block layer
		for (int i = 511; i < 1023; ++i)
			TEST_EQUAL(node_at_flat(t, i), f[i]);

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
			TEST_EQUAL(node_at_flat(t, i), f[i]);
			TEST_EQUAL(node_at_flat(t, merkle_get_sibling(i)), f[merkle_get_sibling(i)]);
		}

		// check the piece layer (only live positions; compact storage
		// returns the implied pad hash at padding positions)
		for (int i = 127; i < 255; ++i)
		{
			int const L = merkle_get_layer(i);
			int const O = i - merkle_layer_start(L);
			// one is the root of the hashes we added, the other is part of the
			// proof anchroing it in the root
			if (i == 127 + piece_index || merkle_get_sibling(i) == 127 + piece_index)
				TEST_EQUAL(node_at_flat(t, i), f[i]);
			else if (!t.is_padding(L, O))
				TEST_CHECK(node_at_flat(t, i).is_all_zeros());
		}

		// check the block layer (only live positions; padding returns
		// the implied pad value, here zero at the leaf layer)
		for (int i = 511; i < 1023; ++i)
		{
			int const L = merkle_get_layer(i);
			int const O = i - merkle_layer_start(L);
			if (i >= 511 + piece_index*blocks_per_piece && i < 511 + piece_index*blocks_per_piece + 4)
				TEST_EQUAL(node_at_flat(t, i), f[i]);
			else if (!t.is_padding(L, O))
				TEST_CHECK(node_at_flat(t, i).is_all_zeros());
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
		if (block == num_blocks - 1)
			TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::hash_failed);
		else
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
		TEST_EQUAL(node_at_flat(t, i), f[i]);

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
		TEST_EQUAL(node_at_flat(t, i), f[i]);

	for (int i = 255; i < 1023; ++i)
	{
		int const L = merkle_get_layer(i);
		int const O = i - merkle_layer_start(L);
		if (!t.is_padding(L, O)) TEST_CHECK(node_at_flat(t, i).is_all_zeros());
	}

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
			TEST_EQUAL(node_at_flat(t, i), f[i]);
	}

	// use a proof that ties the first piece node 3 (since we don't need it all
	// the way to the root).
	auto const result = t.add_hashes(127, pdiff(1), range(f, 127, 4), build_proof(f, 31, 3));
	TEST_CHECK(result);

	auto const& res = *result;
	TEST_EQUAL(res.passed.size(), 0);
	TEST_EQUAL(res.failed.size(), 0);

	for (int i = 127; i < 127 + 4; ++i)
		TEST_CHECK(node_at_flat(t, i) == f[i]);

	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
}

// load_piece_layer transitions the tree out of full_tree mode by replacing
// m_tree with the freshly-validated piece layer. The piece_layer and
// block_layer invariants both require m_block_verified to be empty, so
// load_piece_layer must drop it on the transition even when the caller
// hands it a tree that's been brought into full_tree mode with some bits
// set -- a state set up here by installing the piece layer and then
// verifying piece 0 via per-block set_block calls. The INVARIANT_CHECK
// destructor in load_piece_layer enforces this on the way out, and a
// follow-up set_block exercises the entry-side check too.
TORRENT_TEST(load_piece_layer_clears_verified_bits)
{
	int const blocks_per_piece = 4;
	aux::merkle_tree t(num_blocks, blocks_per_piece, f[0].data());

	// install the piece-layer hashes; add_hashes only ever optimizes down to
	// block_layer/empty_tree, so the tree stays in full_tree mode here with
	// m_block_verified all-false
	auto const piece_result =
		t.add_hashes(127, pdiff(1), range(f, 127, 128), span<sha256_hash const>());
	TEST_CHECK(piece_result);
	if (!piece_result) return;

	// set all four block hashes for piece 0; the last call moves us into
	// full_tree mode and verifies the piece, setting m_block_verified[0..3].
	for (int b = 0; b < blocks_per_piece; ++b)
		t.set_block(b, f[511 + b]);
	TEST_CHECK(t.blocks_verified(0, blocks_per_piece));

	// materialize the piece-layer bytes from the reference
	int const npieces = (num_blocks + blocks_per_piece - 1) / blocks_per_piece;
	int const piece_layer_first = merkle_first_leaf(merkle_num_leafs(npieces));
	std::vector<char> piece_layer_bytes(std::size_t(npieces) * sha256_hash::size());
	for (int i = 0; i < npieces; ++i)
		std::memcpy(piece_layer_bytes.data() + i * int(sha256_hash::size()),
			f[piece_layer_first + i].data(),
			sha256_hash::size());

	TEST_CHECK(t.load_piece_layer(piece_layer_bytes));

	// in piece_layer mode with bpp > 1, no blocks should report as verified
	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));

	// after load_piece_layer the tree is back in the piece_layer-only
	// state, so a single set_block can't yet verify against the sibling.
	auto const result = t.set_block(0, f[511]);
	TEST_CHECK(std::get<0>(result) == aux::merkle_tree::set_block_result::unknown);

	// the set_block above ran allocate_full(), bringing us back to full_tree
	// mode where verified_leafs()/blocks_verified() consult m_block_verified
	// directly (in piece_layer mode they ignore it). allocate_full's resize
	// preserves any pre-existing bits, so had load_piece_layer failed to
	// clear them the stale piece-0 bits would resurface as "verified" here --
	// catching the bug even in builds compiled without invariant checks.
	TEST_CHECK(!t.blocks_verified(0, blocks_per_piece));
	TEST_CHECK(t.verified_leafs() == none_set(num_blocks));
}

// TODO: add test for load_piece_layer()
// TODO: add test for add_hashes() with an odd number of blocks
// TODO: add test for set_block() (setting the last block) with an odd number of blocks

TORRENT_TEST(compact_storage_smaller_than_flat)
{
	// For non-power-of-2 block counts, the compact layout stores fewer
	// hashes than the standard 2*L - 1 nodes.
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	int const compact_size = int(t.build_vector().size());
	int const flat_size = merkle_num_nodes(num_leafs);
	TEST_CHECK(compact_size < flat_size);
	// For 259 blocks: sum of live counts = 1+2+3+5+9+17+33+65+130+259 = 524
	TEST_EQUAL(compact_size, 524);
	TEST_EQUAL(flat_size, 1023);
}

TORRENT_TEST(padding_position_reads_implied_pad)
{
	// In a non-power-of-2 tree, padding positions report the implied pad
	// hash (merkle_pad with the appropriate subtree size) rather than
	// zero. Verify this for an interior padding slot.
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	t.allocate_compact();

	// At L=2 in a 9-layer tree (num_blocks=259), live count is 3 so
	// offset 3 is the first padding slot. The implied pad covers
	// 2^(9 - 2) = 128 zero leaves.
	int const pad_L = 2;
	int const pad_O = 3;
	sha256_hash const expected_pad = merkle_pad(128, 1);
	TEST_EQUAL(t.node_at(pad_L, pad_O), expected_pad);
	TEST_CHECK(t.is_padding(pad_L, pad_O));
	// And a live slot at the same layer is unset (zero):
	TEST_CHECK(!t.is_padding(pad_L, 2));
	TEST_CHECK(t.node_at(pad_L, 2).is_all_zeros());
}

TORRENT_TEST(set_padding_rejects_non_pad_value)
{
	// Writing a non-pad hash to a padding slot is rejected; writing the
	// implied pad (or zero, as a harmless clear) is accepted as a no-op.
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	t.allocate_compact();

	int const pad_L = 2;
	int const pad_O = 3;

	// rejected: a non-pad, non-zero value
	sha256_hash const bogus("01234567890123456789012345678901");
	TEST_CHECK(!t.set(pad_L, pad_O, bogus));

	// accepted: zero (a no-op clear)
	TEST_CHECK(t.set(pad_L, pad_O, sha256_hash{}));

	// accepted: the implied pad value
	TEST_CHECK(t.set(pad_L, pad_O, merkle_pad(128, 1)));

	// after all of that, the slot still reads back as the implied pad
	TEST_EQUAL(t.node_at(pad_L, pad_O), merkle_pad(128, 1));
}

TORRENT_TEST(validate_and_insert_proofs_padding_uncle_rejected)
{
	// A proof chain whose first uncle lands on a padding sibling must
	// supply the implied pad. A non-pad uncle is rejected by set() and
	// the proof-validation walk fails, leaving the tree unchanged.
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	t.allocate_compact();

	// Insert at (L=9, O=258), the rightmost live leaf. Its sibling at
	// (L=9, O=259) is padding; the leaf-layer pad is zero. A non-zero
	// uncle here must be rejected.
	sha256_hash const leaf_hash = f[merkle_first_leaf(num_leafs) + 258];
	std::vector<sha256_hash> bogus_proofs{sha256_hash("01234567890123456789012345678901")};

	TEST_CHECK(!merkle_validate_and_insert_proofs(t, 9, 258, leaf_hash, bogus_proofs));

	// Rollback: the target node and the rejected sibling must both be
	// back to their pre-call state (uninitialized / implied-pad).
	TEST_CHECK(t.node_at(9, 258).is_all_zeros());
	TEST_CHECK(t.node_at(9, 259).is_all_zeros()); // leaf-layer pad is zero
}

TORRENT_TEST(load_sparse_tree_padding_mask_bit_implied_pad)
{
	// load_sparse_tree can carry a mask bit at a padding flat index; the
	// value alongside that bit must match the implied pad. The slot then
	// reads back as the implied pad. (set() silently accepts the matching
	// pad write as a no-op; the actual storage doesn't change.)
	aux::merkle_tree t(num_blocks, 1, f[0].data());

	int const pad_L = 2;
	int const pad_O = 3;
	int const pad_flat = merkle_to_flat_index(pad_L, pad_O);
	sha256_hash const expected_pad = merkle_pad(128, 1);

	std::vector<sha256_hash> sparse{expected_pad};
	bitfield mask(num_nodes, false);
	mask.set_bit(pad_flat);

	t.load_sparse_tree(sparse, mask, empty_verified);

	// Padding read-back is unaffected by whether the sparse load touched
	// it -- the slot is implied.
	TEST_EQUAL(t.node_at(pad_L, pad_O), expected_pad);
}

TORRENT_TEST(build_vector_roundtrip_odd_blocks)
{
	// Round-trip a partial tree through the sparse-vector serialization:
	// load some data, build_sparse_vector, construct a new tree, load,
	// build_vector. The two compact representations must agree.
	aux::merkle_tree t(num_blocks, 1, f[0].data());
	t.load_tree(f, empty_verified);

	auto const compact_before = t.build_vector();

	auto const [sparse, mask] = t.build_sparse_vector();
	aux::merkle_tree t2(num_blocks, 1, f[0].data());
	t2.load_sparse_tree(sparse, mask, empty_verified);

	auto const compact_after = t2.build_vector();
	TEST_CHECK(compact_before == compact_after);
	TEST_EQUAL(int(compact_before.size()), 524);
}
