/*

Copyright (c) 2015, 2017, 2019-2021, Arvid Norberg
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_MERKLE_HPP_INCLUDED
#define TORRENT_MERKLE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/vector.hpp"
#include <vector>
#include <utility> // pair
#include <tuple>

namespace libtorrent {

	struct bitfield;

	namespace aux {
		struct merkle_tree;
	}

	// given layer and offset from the start of the layer, return the nodex
	// index
	TORRENT_EXTRA_EXPORT int merkle_to_flat_index(int layer, int offset);

	// given layer, returns index to the layer's first node
	TORRENT_EXTRA_EXPORT int merkle_layer_start(int layer);

	// given the number of blocks, how many leaves do we need? this rounds up to
	// an even power of 2
	TORRENT_EXTRA_EXPORT int merkle_num_leafs(int);

	// returns the number of nodes in the tree, given the number of leaves
	TORRENT_EXTRA_EXPORT int merkle_num_nodes(int);

	// given the number of leafs in the tree, returns the index to the first
	// leaf
	TORRENT_EXTRA_EXPORT int merkle_first_leaf(int num_leafs);

	// takes the number of leaves and returns the height of the merkle tree.
	// does not include the root node in the layer count. i.e. if there's only a
	// root hash, there are 0 layers. Note that the number of leaves must be
	// valid, i.e. a power of 2.
	TORRENT_EXTRA_EXPORT int merkle_num_layers(int);
	TORRENT_EXTRA_EXPORT int merkle_get_parent(int);
	TORRENT_EXTRA_EXPORT int merkle_get_sibling(int);
	TORRENT_EXTRA_EXPORT int merkle_get_first_child(int);
	TORRENT_EXTRA_EXPORT int merkle_get_first_child(int tree_node, int depth);

	// given a tree and the number of leaves, expect all leaf hashes to be set and
	// compute all other hashes starting with the leaves.
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(span<sha256_hash> tree, int num_leafs, int level_start);
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(span<sha256_hash> tree, int num_leafs);

	// Fill the interior nodes of a subtree of `tree`, given its leaves are
	// already set. `L` and `O` are the (level, offset) of the leftmost leaf
	// of the subtree; `level_size` is the number of contiguous leaves.
	// Hashes upward to the subtree's root.
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(
		aux::merkle_tree& tree, int L, int O, int level_size);

	// fills in nodes that can be computed from a tree with arbitrary nodes set
	// all "orphan" hashes, i.e ones that do not contribute towards computing
	// the root, will be cleared.
	TORRENT_EXTRA_EXPORT void merkle_fill_partial_tree(aux::merkle_tree& tree);

	// Clear a subtree: zero out the `level_size` leaves at (L, O) and all
	// of their ancestors up to (but not including) the subtree's root.
	TORRENT_EXTRA_EXPORT void merkle_clear_tree(
		aux::merkle_tree& tree, int L, int O, int level_size);

	// given the leaf hashes, computes the merkle root hash. The pad is the hash
	// to use for the right-side padding, in case the number of leaves is not a
	// power of two.
	TORRENT_EXTRA_EXPORT sha256_hash merkle_root(span<sha256_hash const> leaves, sha256_hash const& pad = {});

	TORRENT_EXTRA_EXPORT
	sha256_hash merkle_root_scratch(span<sha256_hash const> leaves, int num_leafs
		, sha256_hash pad, std::vector<sha256_hash>& scratch_space);

	// given a flat index, return which layer the node is in
	TORRENT_EXTRA_EXPORT int merkle_get_layer(int idx);
	// given a flat index, return the offset in the layer
	TORRENT_EXTRA_EXPORT int merkle_get_layer_offset(int idx);

	// given "blocks" number of leafs in the full tree (i.e. at the block level)
	// and given "pieces" nodes in the piece layer, compute the pad hash for the
	// piece layer
	TORRENT_EXTRA_EXPORT sha256_hash const& merkle_pad(int blocks, int pieces);

	// Validate `node` against the chain of `uncle_hashes` and insert the
	// proven hashes (the node, the uncles, and the computed parents along
	// the chain) into `target_tree` starting at (target_L, target_O).
	// Returns true on success; on failure, any hashes inserted along the
	// way are rolled back.
	//
	// For example, consider the following tree (target_tree):
	//
	//            R
	//     2              _
	//  _     _        _     1
	//_   _ _   _    N   0 _   _
	// The root R is expected to be known and set in target_tree.
	// if we're inserting the hash N, the uncle hashes provide proof of it being
	// valid by containing 0, 1 and two (as marked in the tree above)
	// Any non-zero hash encountered in target_tree is assumed to be valid, and
	// will terminate the validation early, either successful (if there's a
	// match) or unsuccessful (if there's a mismatch).
	TORRENT_EXTRA_EXPORT
	bool merkle_validate_and_insert_proofs(aux::merkle_tree& target_tree,
		int target_L,
		int target_O,
		sha256_hash const& node,
		span<sha256_hash const> uncle_hashes);

	TORRENT_EXTRA_EXPORT
	bool merkle_validate_node(sha256_hash const& left, sha256_hash const& right
		, sha256_hash const& parent);

	// Validate the standard-layout flat tree `src` against `expected_root`
	// and copy validated subtrees into `dst`'s compact storage.
	// `verified_leafs` is set (one bit per leaf) for every leaf whose hash
	// was validated by a parent that itself was already known in dst.
	TORRENT_EXTRA_EXPORT
	void merkle_validate_copy(span<sha256_hash const> src,
		aux::merkle_tree& dst,
		sha256_hash const& expected_root,
		bitfield& verified_leafs);

	TORRENT_EXTRA_EXPORT
	bool merkle_validate_single_layer(span<sha256_hash const> tree);

	// Given a block index (offset in the leaf layer) and a tree, return the
	// largest verifiable subtree containing that block: the leaf-layer range
	// [leafs_start, leafs_start + leafs_size) and the (root_L, root_O) of
	// the node that needs to be known in `tree` to validate the subtree.
	// `num_valid_leafs` is the count of live (non-padding) leaves.
	TORRENT_EXTRA_EXPORT
	std::tuple<int, int, int, int> merkle_find_known_subtree(
		aux::merkle_tree const& tree, int block_index, int num_valid_leafs);
}

#endif
