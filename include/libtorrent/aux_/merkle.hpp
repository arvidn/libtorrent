/*

Copyright (c) 2003-2014, Arvid Norberg
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
#include <vector>
#include <utility> // pair

namespace libtorrent {

	TORRENT_EXTRA_EXPORT int merkle_to_flat_index(int layer, int offset);

	// given the number of blocks, how many leaves do we need? this rounds up to
	// an even power of 2
	TORRENT_EXTRA_EXPORT int merkle_num_leafs(int);

	// returns the number of nodes in the tree, given the number of leaves
	TORRENT_EXTRA_EXPORT int merkle_num_nodes(int);

	// takes the number of leaves and returns the height of the merkle tree.
	// does not include the root node in the layer count. i.e. if there's only a
	// root hash, there are 0 layerrs. Note that the number of leaves must be
	// valid, i.e. a power of 2.
	TORRENT_EXTRA_EXPORT int merkle_num_layers(int);
	TORRENT_EXTRA_EXPORT int merkle_get_parent(int);
	TORRENT_EXTRA_EXPORT int merkle_get_sibling(int);
	TORRENT_EXTRA_EXPORT int merkle_get_first_child(int);

	// given a tree and the number of leaves, expect all leaf hashes to be set and
	// compute all other hashes starting with the leaves.
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(span<sha256_hash> tree, int num_leafs, int level_start);
	TORRENT_EXTRA_EXPORT void merkle_fill_tree(span<sha256_hash> tree, int num_leafs);

	// given a merkle tree (`tree`), clears all hashes in the range of nodes:
	// [ level_start, level_start+ num_leafs), as well as all of their parents,
	// within the sub-tree. It does not clear the root of the sub-tree.
	// see unit test for examples.
	TORRENT_EXTRA_EXPORT void merkle_clear_tree(span<sha256_hash> tree, int num_leafs, int level_start);

	// given the leaf hashes, computes the merkle root hash. The pad is the hash
	// to use for the right-side padding, in case the number of leaves is not a
	// power of two.
	TORRENT_EXTRA_EXPORT sha256_hash merkle_root(span<sha256_hash const> leaves, sha256_hash const& pad = {});

	// given a flat index, return which layer the node is in
	TORRENT_EXTRA_EXPORT int merkle_get_layer(int idx);
	// given a flat index, return the offset in the layer
	TORRENT_EXTRA_EXPORT int merkle_get_layer_offset(int idx);
}

#endif
