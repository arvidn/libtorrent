/*

Copyright (c) 2015, 2017, 2019-2021, Arvid Norberg
Copyright (c) 2015, Mike Tzou
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/ffs.hpp" // for log2p1
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/bitfield.hpp"

namespace libtorrent {

	int merkle_layer_start(int const layer)
	{
		TORRENT_ASSERT(layer >= 0);
		TORRENT_ASSERT(layer < int(sizeof(int) * 8));
		return (1 << layer) - 1;
	}

	int merkle_to_flat_index(int const layer, int const offset)
	{
		TORRENT_ASSERT(layer >= 0);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(layer < int(sizeof(int) * 8));
		return merkle_layer_start(layer) + offset;
	}

	int merkle_get_parent(int const tree_node)
	{
		// node 0 doesn't have a parent
		TORRENT_ASSERT(tree_node > 0);
		return (tree_node - 1) / 2;
	}

	int merkle_get_sibling(int const tree_node)
	{
		// node 0 doesn't have a sibling
		TORRENT_ASSERT(tree_node > 0);
		// even numbers have their sibling to the left
		// odd numbers have their sibling to the right
		return tree_node + ((tree_node&1)?1:-1);
	}

	int merkle_get_first_child(int const tree_node)
	{
		return tree_node * 2 + 1;
	}

	int merkle_get_first_child(int const tree_node, int depth)
	{
		return ((tree_node + 1) << depth) - 1;
	}

	int merkle_num_nodes(int const leafs)
	{
		TORRENT_ASSERT(leafs > 0);
		TORRENT_ASSERT(leafs <= (std::numeric_limits<int>::max() / 2) + 1);
		TORRENT_ASSERT((leafs & (leafs - 1)) == 0);
		// This is a way to calculate: (leafs << 1) - 1 without requiring an extra
		// bit in the far left. The first 1 we subtract is worth 2 after we
		// multiply by 2, so by just adding back one, we have effectively
		// subtracted one from the result of multiplying by 2
		return ((leafs - 1) << 1) + 1;
	}

	int merkle_first_leaf(int num_leafs)
	{
		// num_leafs must be a power of 2
		TORRENT_ASSERT(((num_leafs - 1) & num_leafs) == 0);
		TORRENT_ASSERT(num_leafs > 0);
		return num_leafs - 1;
	}

	int merkle_num_leafs(int const blocks)
	{
		TORRENT_ASSERT(blocks > 0);
		TORRENT_ASSERT(blocks <= std::numeric_limits<int>::max() / 2);
		// round up to nearest 2 exponent
		int ret = 1;
		while (blocks > ret) ret <<= 1;
		return ret;
	}

	int merkle_num_layers(int leaves)
	{
		// leaves must be a power of 2
		TORRENT_ASSERT((leaves & (leaves - 1)) == 0);
		int layers = 0;
		while (leaves > 1)
		{
			++layers;
			leaves >>= 1;
		}
		return layers;
	}

	void merkle_fill_tree(span<sha256_hash> tree, int const num_leafs)
	{
		merkle_fill_tree(tree, num_leafs, merkle_num_nodes(num_leafs) - num_leafs);
	}

	void merkle_fill_tree(span<sha256_hash> tree, int const num_leafs, int level_start)
	{
		TORRENT_ASSERT(level_start >= 0);
		TORRENT_ASSERT(num_leafs >= 1);

		int level_size = num_leafs;
		hasher256 h;
		while (level_size > 1)
		{
			int parent = merkle_get_parent(level_start);
			for (int i = level_start; i < level_start + level_size; i += 2, ++parent)
			{
				h.reset();
				h.update(tree[i]);
				h.update(tree[i + 1]);
				tree[parent] = h.final();
			}
			level_start = merkle_get_parent(level_start);
			level_size /= 2;
		}
		TORRENT_ASSERT(level_size == 1);
	}

	void merkle_fill_tree(aux::merkle_tree& tree, int L, int O, int level_size)
	{
		TORRENT_ASSERT(L >= 0);
		TORRENT_ASSERT(L <= tree.num_layers());
		TORRENT_ASSERT(O >= 0);
		TORRENT_ASSERT(level_size >= 1);

		while (level_size > 1)
		{
			for (int i = 0; i < level_size; i += 2)
			{
				tree.set(
					L - 1, (O + i) / 2, tree.hash_pair(tree.get(L, O + i), tree.get(L, O + i + 1)));
			}
			--L;
			O >>= 1;
			level_size /= 2;
		}
		TORRENT_ASSERT(level_size == 1);
	}

	void merkle_fill_partial_tree(aux::merkle_tree& tree)
	{
		int const N = tree.num_layers();

		// we do two passes over the tree, first to compute all the missing
		// "interior" hashes. Then to clear all the ones that don't have a
		// parent (i.e. "orphan" hashes). We clear them since we can't validate
		// them against the root, which mean they may be incorrect.
		for (int L = N; L > 0; --L)
		{
			int const layer_size = 1 << L;
			for (int O = 0; O < layer_size; O += 2)
			{
				sha256_hash const left = tree.get(L, O);
				sha256_hash const right = tree.get(L, O + 1);
				if (left.is_all_zeros() || right.is_all_zeros()) continue;
				tree.set(L - 1, O / 2, tree.hash_pair(left, right));
			}
		}

		for (int L = 1; L <= N; ++L)
		{
			int const layer_size = 1 << L;
			for (int O = 0; O < layer_size; O += 2)
			{
				if (tree.get(L - 1, O / 2).is_all_zeros())
				{
					// if the parent is all zeros, the validation chain up to
					// the root is broken, and these cannot be validated
					tree.set(L, O, sha256_hash{});
					tree.set(L, O + 1, sha256_hash{});
				}
				else if (tree.get(L, O + 1).is_all_zeros())
				{
					// if the sibling is all zeros, this hash cannot be validated
					tree.set(L, O, sha256_hash{});
				}
				else if (tree.get(L, O).is_all_zeros())
				{
					// if this hash is all zeros, the sibling hash cannot be validated
					tree.set(L, O + 1, sha256_hash{});
				}
			}
		}
	}

	void merkle_clear_tree(aux::merkle_tree& tree, int L, int O, int level_size)
	{
		TORRENT_ASSERT(L >= 0);
		TORRENT_ASSERT(L <= tree.num_layers());
		TORRENT_ASSERT(O >= 0);
		TORRENT_ASSERT(level_size >= 1);

		for (;;)
		{
			for (int i = 0; i < level_size; ++i)
				tree.set(L, O + i, sha256_hash{});
			if (level_size == 1) break;
			--L;
			O >>= 1;
			level_size /= 2;
		}
		TORRENT_ASSERT(level_size == 1);
	}

	// compute the merkle tree root, given the leaves and the has to use for
	// padding
	sha256_hash merkle_root(span<sha256_hash const> const leaves, sha256_hash const& pad)
	{
		int const num_leafs = merkle_num_leafs(int(leaves.size()));
		aux::vector<sha256_hash> merkle_tree;
		return merkle_root_scratch(leaves, num_leafs, pad, merkle_tree);
	}

	// compute the merkle tree root, given the leaves and the has to use for
	// padding
	sha256_hash merkle_root_scratch(span<sha256_hash const> leaves
		, int num_leafs, sha256_hash pad
		, std::vector<sha256_hash>& scratch_space)
	{
		TORRENT_ASSERT(((num_leafs - 1) & num_leafs) == 0);

		scratch_space.resize(std::size_t(leaves.size() + 1) / 2);
		TORRENT_ASSERT(num_leafs > 0);

		if (num_leafs == 1) return leaves[0];

		// reuse a single hasher rather than constructing and destructing
		// one per pair-hash; the underlying SHA-256 context allocation
		// (e.g. EVP_MD_CTX*) is non-trivial. reset() puts the hasher back
		// into a just-default-constructed state after each final().
		hasher256 h;
		while (num_leafs > 1)
		{
			int i = 0;
			for (; i < int(leaves.size()) / 2; ++i)
			{
				h.reset();
				scratch_space[std::size_t(i)] =
					h.update(leaves[i * 2]).update(leaves[i * 2 + 1]).final();
			}
			if (leaves.size() & 1)
			{
				// if we have an odd number of leaves, compute the boundary hash
				// here, that spans both a payload-hash and a pad hash
				h.reset();
				scratch_space[std::size_t(i)] = h.update(leaves[i * 2]).update(pad).final();
				++i;
			}
			// we don't have to copy any pad hashes into memory, they are implied
			// just keep track of the current layer's pad hash
			h.reset();
			pad = h.update(pad).update(pad).final();

			// step one level up
			leaves = span<sha256_hash const>(scratch_space.data(), i);
			num_leafs /= 2;
		}

		return scratch_space[0];
	}

	// returns the layer the given offset into the tree falls into.
	// Layer 0 is the root of the tree, layer 1 is the two hashes below the
	// root, and so on.
	int merkle_get_layer(int idx)
	{
		TORRENT_ASSERT(idx >= 0);
		int layer = 1;
		while (idx > (1 << layer) - 2) layer++;
		return layer - 1;
	}

	// returns the start of the layer, offset `idx` falls into.
	int merkle_get_layer_offset(int idx)
	{
		return idx - ((1 << merkle_get_layer(idx)) - 1);
	}

	// generates the pad hash for the tree level with "pieces" nodes, given the
	// full tree has "blocks" number of blocks. Both must be powers of two.
	// Returns a reference into a static table indexed by log2(blocks/pieces).
	sha256_hash const& merkle_pad(int blocks, int pieces)
	{
		TORRENT_ASSERT(blocks >= pieces);
		TORRENT_ASSERT(pieces > 0);
		TORRENT_ASSERT((blocks & (blocks - 1)) == 0);
		TORRENT_ASSERT((pieces & (pieces - 1)) == 0);

		// 32 exceeds the maximum tree depth this library builds (blocks <= 2^30)
		constexpr int max_depth = 32;
		static auto const table = [] {
			aux::array<sha256_hash, max_depth + 1, int> t{};
			for (int i = 1; i <= max_depth; ++i)
				t[i] = hasher256().update(t[i - 1]).update(t[i - 1]).final();
			return t;
		}();

		int const d = int(aux::log2p1(aux::numeric_cast<std::uint32_t>(blocks)))
			- int(aux::log2p1(aux::numeric_cast<std::uint32_t>(pieces)));
		TORRENT_ASSERT(d >= 0 && d <= max_depth);
		return table[d];
	}

	bool merkle_validate_and_insert_proofs(aux::merkle_tree& target_tree,
		int const target_L,
		int const target_O,
		sha256_hash const& node,
		span<sha256_hash const> uncle_hashes)
	{
		if (target_tree.get(target_L, target_O) == node) return true;

		if (!target_tree.get(target_L, target_O).is_all_zeros()) return false;

		if (uncle_hashes.empty()) return false;

		int cursor_L = target_L;
		int cursor_O = target_O;
		if (!target_tree.set(cursor_L, cursor_O, node)) return false;
		for (auto const& proof : uncle_hashes)
		{
			int const sibling_O = cursor_O ^ 1;
			// the sibling is either uninitialized (zero) or, in compact
			// storage, a padding slot whose implied value is the layer's
			// pad hash. set() rejects a non-pad write into a padding slot,
			// which is how a malicious proof aimed at a padding sibling is
			// caught.
			TORRENT_ASSERT(target_tree.get(cursor_L, sibling_O).is_all_zeros()
				|| target_tree.is_padding(cursor_L, sibling_O));
			if (!target_tree.set(cursor_L, sibling_O, proof)) break;
			int const left_O = cursor_O & ~1;
			sha256_hash const hash = target_tree.hash_pair(
				target_tree.get(cursor_L, left_O), target_tree.get(cursor_L, left_O + 1));
			--cursor_L;
			cursor_O >>= 1;
			if (target_tree.get(cursor_L, cursor_O) == hash) return true;
			if (!target_tree.get(cursor_L, cursor_O).is_all_zeros()) break;
			if (!target_tree.set(cursor_L, cursor_O, hash)) break;
		}

		// we get here if we never reached a known hash in the tree, i.e. the
		// uncle_hashes failed to prove the specified node hash.
		// we now need to clear up all the hashes we've inserted into the tree.
		// Always clear the target (we wrote it before the loop). Then walk
		// upward clearing each (sibling, parent) pair we wrote, stopping
		// before `cursor` (whose slot we did not write -- either it has a
		// pre-existing non-matching value, or set() rejected it as a
		// padding mismatch).
		int clear_L = target_L;
		int clear_O = target_O;
		target_tree.set(clear_L, clear_O, sha256_hash{});
		while (clear_L > cursor_L)
		{
			target_tree.set(clear_L, clear_O ^ 1, sha256_hash{});
			--clear_L;
			clear_O >>= 1;
			if (clear_L > cursor_L) target_tree.set(clear_L, clear_O, sha256_hash{});
		}
		target_tree.set(cursor_L, cursor_O, sha256_hash{});
		return false;
	}

	bool merkle_validate_node(sha256_hash const& left, sha256_hash const& right
		, sha256_hash const& parent)
	{
		hasher256 h;
		h.update(left);
		h.update(right);
		return (h.final() == parent);
	}

	void merkle_validate_copy(span<sha256_hash const> const src,
		aux::merkle_tree& dst,
		sha256_hash const& expected_root,
		bitfield& verified_leafs)
	{
		if (src.empty()) return;
		if (src[0] != expected_root) return;
		int const N = dst.num_layers();
		int const num_leafs = 1 << N;
		TORRENT_ASSERT(int(src.size()) == 2 * num_leafs - 1);

		dst.set(0, 0, src[0]);
		for (int L = 0; L < N; ++L)
		{
			int const layer_size = 1 << L;
			int const layer_flat = (1 << L) - 1;
			for (int O = 0; O < layer_size; ++O)
			{
				if (dst.get(L, O).is_all_zeros()) continue;
				int const left_flat = 2 * (layer_flat + O) + 1;
				int const right_flat = left_flat + 1;
				// inlined merkle_validate_node so we reuse dst's scratch hasher
				if (dst.hash_pair(src[left_flat], src[right_flat]) != dst.get(L, O)) continue;

				dst.set(L + 1, 2 * O, src[left_flat]);
				dst.set(L + 1, 2 * O + 1, src[right_flat]);

				if (L + 1 == N)
				{
					int const block_idx = 2 * O;
					if (block_idx < verified_leafs.size())
					{
						verified_leafs.set_bit(block_idx);
						// the right child may be the first block of padding hash,
						// in which case it's not part of the verified bitfield
						if (block_idx + 1 < verified_leafs.size())
							verified_leafs.set_bit(block_idx + 1);
					}
				}
			}
		}
	}

	bool merkle_validate_single_layer(span<sha256_hash const> tree)
	{
		if (tree.size() == 1) return true;
		int const num_leafs = int((tree.size() + 1) / 2);
		int const end = int(tree.size());
		TORRENT_ASSERT((num_leafs & (num_leafs - 1)) == 0);

		int idx = merkle_first_leaf(num_leafs);
		TORRENT_ASSERT(idx >= 1);

		// inlined merkle_validate_node so the SHA-256 context is reused
		// across all pair checks in this layer.
		hasher256 h;
		while (idx < end)
		{
			h.reset();
			if (h.update(tree[idx]).update(tree[idx + 1]).final() != tree[merkle_get_parent(idx)])
				return false;
			idx += 2;
		}
		return true;
	}

	std::tuple<int, int, int, int> merkle_find_known_subtree(
		aux::merkle_tree const& tree, int const block_index, int const num_valid_leafs)
	{
		// find the largest block of leafs from a single subtree we know the hashes of
		int const N = tree.num_layers();
		int leafs_start = block_index;
		int leafs_size = 1;
		int root_L = N;
		int root_O = block_index ^ 1;
		for (int i = block_index;; i >>= 1)
		{
			int const first_check = leafs_start + ((i & 1) ? -leafs_size : leafs_size);
			for (int j = 0; j < std::min(leafs_size, num_valid_leafs - first_check); ++j)
			{
				if (tree.get(N, first_check + j).is_all_zeros())
					return std::make_tuple(leafs_start, leafs_size, root_L, root_O);
			}
			if (i & 1) leafs_start -= leafs_size;
			leafs_size *= 2;
			--root_L;
			root_O >>= 1;
			// if an inner node is known then its parent must be known too
			// so if the root is known the next sibling subtree should already
			// be computed if all of its leafs have valid hashes
			if (!tree.get(root_L, root_O).is_all_zeros()) break;
			TORRENT_ASSERT(root_L > 0);
			TORRENT_ASSERT(leafs_start >= 0);
			TORRENT_ASSERT(leafs_size <= 1 << N);
		}

		TORRENT_ASSERT(leafs_start >= 0);
		TORRENT_ASSERT(leafs_start < 1 << N);
		TORRENT_ASSERT(leafs_start + leafs_size > block_index);

		return std::make_tuple(leafs_start, leafs_size, root_L, root_O);
	}
}

