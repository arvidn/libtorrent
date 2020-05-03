/*

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

#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/vector.hpp"

namespace libtorrent {
namespace aux {

	merkle_tree::merkle_tree(int const num_blocks, char const* r)
		: m_root(r)
		, m_tree(merkle_num_nodes(merkle_num_leafs(num_blocks)))
		, m_num_blocks(num_blocks)
	{
		TORRENT_ASSERT(m_root != nullptr);
		m_tree[0] = root();
	}

	sha256_hash merkle_tree::root() const { return sha256_hash(m_root); }

	void merkle_tree::load_tree(span<sha256_hash const> t)
	{
		if (t.empty()) return;
		if (root() != t[0]) return;
		if (size() != static_cast<std::size_t>(t.size())) return;

		// TODO: If t has a complete block layer, just store that
		// otherwise, if t has a complete piece layer, just store that

		m_tree.assign(t.begin(), t.end());
	}

	// returns false if the piece layer fails to validate against the root hash
	bool merkle_tree::load_piece_layer(span<char const> piece_layer)
	{
		int const num_pieces = static_cast<int>(piece_layer.size() / sha256_hash::size());
		int const piece_layer_size = merkle_num_leafs(num_pieces);
		int const first_piece_node = merkle_num_nodes(piece_layer_size) - piece_layer_size;
		auto const num_leafs = (m_tree.end_index() + 1) / 2;

		auto const r = root();

		sha256_hash const pad_hash = merkle_pad(num_leafs, piece_layer_size);

		for (int n = 0; n < num_pieces; ++n)
			m_tree[first_piece_node + n].assign(piece_layer.data() + n * sha256_hash::size());
		for (int n = num_pieces; n < piece_layer_size; ++n)
			m_tree[first_piece_node + n] = pad_hash;

		fill(piece_layer_size);

		if (m_tree[0] != r)
		{
			std::fill(m_tree.begin(), m_tree.end(), sha256_hash{});
			m_tree[0] = r;
			return false;
		}
		return true;
	}

	std::map<piece_index_t, std::vector<int>> merkle_tree::add_hashes(
		int dest_start_idx, int const blocks_per_piece
		, span<sha256_hash const> tree)
	{
		std::map<piece_index_t, std::vector<int>> failed_blocks;

		// first fill in the subtree of known hashes from the base layer

		auto const num_leafs = (m_tree.end_index() + 1) / 2;
		auto const first_leaf = m_tree.end_index() - num_leafs;

		// the leaf nodes in the passed-in "tree"
		int const count = int((tree.size() + 1) / 2);

		// this is the start of the leaf layer of "tree". We'll use this
		// variable to step upwards towards the root
		int source_start_idx = int(tree.size()) - count;

		// the tree is expected to be consistent
		TORRENT_ASSERT(merkle_root(span<sha256_hash const>(tree).last(count)) == tree[0]);

		for (int layer_size = count; layer_size != 0; layer_size /= 2)
		{
			for (int i = 0; i < layer_size; ++i)
			{
				int const dst_idx = dest_start_idx + i;
				int const src_idx = source_start_idx + i;
				if (has_node(dst_idx) && m_tree[dst_idx] != tree[src_idx])
				{
					// this must be a block hash because inner nodes are not filled in until
					// they can be verified. This assert ensures we're at the
					// leaf layer of the file tree
					TORRENT_ASSERT(dst_idx >= first_leaf);

					// TODO: blocks_per_piece is guaranteed to be a power of 2,
					// so this could just be a shift and AND
					std::div_t const pos = std::div(dst_idx - first_leaf, blocks_per_piece);
					failed_blocks[piece_index_t{pos.quot}].push_back(pos.rem);
				}

				m_tree[dst_idx] = tree[src_idx];
			}
			if (layer_size == 1) break;
			dest_start_idx = merkle_get_parent(dest_start_idx);
			source_start_idx = merkle_get_parent(source_start_idx);
		}
		return failed_blocks;
	}

	void merkle_tree::add_proofs(int dest_start_idx
		, span<std::pair<sha256_hash, sha256_hash> const> proofs)
	{
		// now copy the string of proof hashes
		for (auto proof : proofs)
		{
			int const offset = dest_start_idx & 1;
			m_tree[dest_start_idx + offset - 1] = proof.first;
			m_tree[dest_start_idx + offset] = proof.second;
			dest_start_idx = merkle_get_parent(dest_start_idx);
		}
	}

	std::vector<piece_index_t> merkle_tree::check_pieces(int const base
		, int const index, int const blocks_per_piece, int const file_piece_offset
		, span<sha256_hash const> hashes)
	{
		std::vector<piece_index_t> passed_pieces;

		// blocks per piece must be a power of 2
		TORRENT_ASSERT((blocks_per_piece & (blocks_per_piece - 1)) == 0);

		int const count = static_cast<int>(hashes.size());
		auto const file_num_leafs = (m_tree.end_index() + 1) / 2;
		auto const file_first_leaf = m_tree.end_index() - file_num_leafs;
		int const first_piece = file_first_leaf / blocks_per_piece;

		int const base_layer_index = merkle_num_layers(file_num_leafs) - base;
		int const base_layer_start = merkle_to_flat_index(base_layer_index, index);

		// it may now be possible to verify the hashes of previously received blocks
		// try to verify as many child nodes of the received hashes as possible
		for (int i = 0; i < count; ++i)
		{
			int const piece = index + i;
			if (!m_tree[merkle_get_first_child(first_piece + piece)].is_all_zeros()
				&& !m_tree[merkle_get_first_child(first_piece + piece) + 1].is_all_zeros())
			{
				// this piece is already verified
				continue;
			}

			int const first_leaf = piece << base;
			int const num_leafs = 1 << base;

			bool done = false;
			for (int j = 0; j < std::min(num_leafs, m_num_blocks - first_leaf); ++j)
			{
				if (m_tree[file_first_leaf + first_leaf + j].is_all_zeros())
				{
					done = true;
					break;
				}
			}
			if (done) continue;

			merkle_fill_tree(m_tree, num_leafs, file_first_leaf + first_leaf);
			if (m_tree[base_layer_start + i] != hashes[i])
			{
				merkle_clear_tree(m_tree, num_leafs / 2, merkle_get_parent(file_first_leaf + first_leaf));
				m_tree[base_layer_start + i] = hashes[i];
				TORRENT_ASSERT(num_leafs == blocks_per_piece);
				//verify_block_hashes(m_files.file_offset(req.file) / m_files.piece_length() + index);
				// TODO: add to failed hashes
			}
			else
			{
				passed_pieces.push_back(piece_index_t{file_piece_offset + piece});
			}
		}
		return passed_pieces;
	}

	std::size_t merkle_tree::size() const
	{
		return static_cast<std::size_t>(merkle_num_nodes(merkle_num_leafs(m_num_blocks)));
	}

	bool merkle_tree::has_node(int const idx) const
	{
		return !m_tree[idx].is_all_zeros();
	}

	bool merkle_tree::compare_node(int const idx, sha256_hash const& h) const
	{
		return m_tree[idx] == h;
	}

	std::vector<sha256_hash> merkle_tree::build_vector() const
	{
		std::vector<sha256_hash> ret(m_tree.begin(), m_tree.end());
		return ret;
	}

	void merkle_tree::fill(int const piece_layer_size)
	{
		merkle_fill_tree(m_tree, piece_layer_size);
	}

	void merkle_tree::fill(int const piece_layer_size, int const level_start)
	{
		merkle_fill_tree(m_tree, piece_layer_size, level_start);
	}

	void merkle_tree::clear(int num_leafs, int level_start)
	{
		merkle_clear_tree(m_tree, num_leafs, level_start);
	}

}
}
