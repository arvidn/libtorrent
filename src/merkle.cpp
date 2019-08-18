/*

Copyright (c) 2015, 2017, 2019, Arvid Norberg
Copyright (c) 2015, Mike Tzou
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

#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/vector.hpp"

namespace libtorrent {

	int merkle_to_flat_index(int const layer, int const offset)
	{
		TORRENT_ASSERT(layer >= 0);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(layer < int(sizeof(int) * 8));
		return (1 << layer) - 1 + offset;
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

	int merkle_num_nodes(int const leafs)
	{
		TORRENT_ASSERT(leafs > 0);
		TORRENT_ASSERT(leafs <= (std::numeric_limits<int>::max() / 2) + 1);
		// This is a way to calculate: (leafs << 1) - 1 without requiring an extra
		// bit in the far left. The first 1 we subtract is worth 2 after we
		// multiply by 2, so by just adding back one, we have effectively
		// subtracted one from the result of multiplying by 2
		return ((leafs - 1) << 1) + 1;
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
		while (level_size > 1)
		{
			int parent = merkle_get_parent(level_start);
			for (int i = level_start; i < level_start + level_size; i += 2, ++parent)
			{
				hasher256 h;
				h.update(tree[i]);
				h.update(tree[i + 1]);
				tree[parent] = h.final();
			}
			level_start = merkle_get_parent(level_start);
			level_size /= 2;
		}
		TORRENT_ASSERT(level_size == 1);
	}

	void merkle_clear_tree(span<sha256_hash> tree, int const num_leafs, int level_start)
	{
		TORRENT_ASSERT(num_leafs >= 1);
		TORRENT_ASSERT(level_start > 0);
		TORRENT_ASSERT(level_start < tree.size());
		TORRENT_ASSERT(level_start + num_leafs <= tree.size());
		// the range of nodes must be within a single level
		TORRENT_ASSERT(merkle_get_layer(level_start) == merkle_get_layer(level_start + num_leafs - 1));

		int level_size = num_leafs;
		for (;;)
		{
			for (int i = level_start; i < level_start + level_size; ++i)
				tree[i].clear();
			if (level_size == 1) break;
			level_start = merkle_get_parent(level_start);
			level_size /= 2;
		}
		TORRENT_ASSERT(level_size == 1);
	}

	sha256_hash merkle_root(span<sha256_hash const> leaves, sha256_hash const& pad)
	{
		int const num_blocks = int(leaves.size());
		int const num_leafs = merkle_num_leafs(num_blocks);
		int const num_nodes = merkle_num_nodes(num_leafs);
		int const first_leaf = num_nodes - num_leafs;
		aux::vector<sha256_hash> merkle_tree(num_nodes);
		for (int i = 0; i < num_blocks; ++i)
			merkle_tree[first_leaf + i] = leaves[i];
		for (int i = num_blocks; i < num_leafs; ++i)
			merkle_tree[first_leaf + i] = pad;

		merkle_fill_tree(merkle_tree, num_leafs);
		return merkle_tree[0];
	}

	int merkle_get_layer(int idx)
	{
		TORRENT_ASSERT(idx >= 0);
		int layer = 1;
		while (idx > (1 << layer) - 2) layer++;
		return layer - 1;
	}

	int merkle_get_layer_offset(int idx)
	{
		return idx - ((1 << merkle_get_layer(idx)) - 1);
	}
}

