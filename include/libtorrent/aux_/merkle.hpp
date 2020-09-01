/*

Copyright (c) 2015, 2017, 2019-2020, Arvid Norberg
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

#ifndef TORRENT_MERKLE_HPP_INCLUDED
#define TORRENT_MERKLE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/vector.hpp"
#include <vector>
#include <utility> // pair

namespace libtorrent {

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

	// given a tree and the number of leaves, expect all leaf hashes to be set and
	// compute all other hashes starting with the leaves.
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(span<sha256_hash> tree, int num_leafs, int level_start);
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(span<sha256_hash> tree, int num_leafs);

	// fills in nodes that can be computed from a tree with arbitrary nodes set
	// all "orphan" hashes, i.e ones that do not contribute towards computing
	// the root, will be cleared.
	TORRENT_EXTRA_EXPORT void merkle_fill_partial_tree(span<sha256_hash> tree);

	// given a merkle tree (`tree`), clears all hashes in the range of nodes:
	// [ level_start, level_start+ num_leafs), as well as all of their parents,
	// within the sub-tree. It does not clear the root of the sub-tree.
	// see unit test for examples.
	TORRENT_EXTRA_EXPORT void merkle_clear_tree(span<sha256_hash> tree, int num_leafs, int level_start);

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
	TORRENT_EXTRA_EXPORT sha256_hash merkle_pad(int blocks, int pieces);

	// Computes the path of hashes starting with "hash" given sibling and
	// uncle hashes in "hashes". "index" is the index to the hash we're
	// validating in its layer. This is necessary to know of the sibling is to
	// the left or right.
	// The function returns the path of siblings going up, as well as the final
	// hash above the last pair of sibling hashes returned. The caller needs to
	// verify the returned hash against the known hash in the tree.
	TORRENT_EXTRA_EXPORT
	std::pair<aux::vector<std::pair<sha256_hash, sha256_hash>>, sha256_hash>
	merkle_check_proofs(sha256_hash hash, span<sha256_hash const> hashes, int index);

	TORRENT_EXTRA_EXPORT
	bool merkle_validate_proofs(int start_idx
		, span<std::pair<sha256_hash, sha256_hash> const> proofs);

	TORRENT_EXTRA_EXPORT
	bool merkle_validate_node(sha256_hash const& left, sha256_hash const& right
		, sha256_hash const& parent);

	// validates hashes from src and copies the valid ones to dst given root as
	// the expected root of the tree (i.e. index 0)
	// src and dst must be the same size. dst is expected to be initialized
	// cleared (or only have valid hashes set), this function will not clear
	// hashes in dst that are invalid in src.
	TORRENT_EXTRA_EXPORT
	void merkle_validate_copy(span<sha256_hash const> src, span<sha256_hash> dst
		, sha256_hash const& root);

	TORRENT_EXTRA_EXPORT
	bool merkle_validate_single_layer(span<sha256_hash const> tree);

	// given a leaf index (0-based index in the leaf layer) and a tree, return
	// the leafs_start, leafs_size and root_index representing a subtree that
	// can be validated. The block_index and leaf_size is the range of the leaf
	// layer that can be verified, and the root_index is the node that needs to
	// be known in (tree) to do so. The num_valid_leafs specifies how many of
	// the leafs that are actually *supposed* to be non-zero. Any leafs beyond
	// thses are padding and expected to be zero.
	// The caller must validate the hash at root_index.
	TORRENT_EXTRA_EXPORT
	std::tuple<int, int, int> merkle_find_known_subtree(span<sha256_hash const> const tree
		, int block_index, int num_valid_leafs);
}

#endif
