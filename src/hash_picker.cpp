/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019-2020, Steven Siloti
Copyright (c) 2019-2020, Arvid Norberg
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

#include "libtorrent/hash_picker.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent
{
	namespace
	{
		time_duration const min_request_interval = seconds(3);
	}

/*
merkle tree for a file:

             ^            x
proof_layer  |    x                x
       ^      x       [****************]
       |    x   x   x   x    x   x   x   x
  base |   x x x x x x x x  x x x x x x x x  <- block hash layer, leaves
              ------->
              index
                      ----------------->
                      count
*/

bool validate_hash_request(hash_request const& hr, file_storage const& fs)
{
	// limit the size of the base layer to something reasonable
	// Blocks are requested for an entire piece so this limit
	// effectivly caps the piece size we can handle. A limit of 8192
	// corresponds to a piece size of 128MB.

	if (hr.file < file_index_t{0}
		|| hr.file >= fs.end_file()
		|| hr.base < 0
		|| hr.index < 0
		|| hr.count <= 0
		|| hr.count > 8192
		|| hr.proof_layers < 0)
		return false;

	int const num_leafs = merkle_num_leafs(fs.file_num_blocks(hr.file));
	int const num_layers = merkle_num_layers(num_leafs);

	if (hr.base >= num_layers) return false;

	// the number of hashes at the specified level
	int const level_size = num_leafs >> hr.base;

	// [index, index + count] must be within the number of nodes at the specified
	// level
	if (hr.index >= level_size || hr.index + hr.count > level_size)
		return false;

	if (hr.proof_layers >= num_layers - hr.base) return false;

	return true;
}

	hash_picker::hash_picker(file_storage const& files
		, aux::vector<aux::merkle_tree, file_index_t>& trees
		, aux::vector<std::vector<bool>, file_index_t> verified
		, bool all_verified)
		: m_files(files)
		, m_merkle_trees(trees)
		, m_hash_verified(std::move(verified))
		, m_piece_layer(merkle_num_layers(files.piece_length() / default_block_size))
		, m_piece_tree_root_layer(m_piece_layer + merkle_num_layers(512))
	{
		m_hash_verified.resize(trees.size());
		m_piece_hash_requested.resize(trees.size());
		for (file_index_t f(0); f != m_files.end_file(); ++f)
		{
			if (m_files.pad_file_at(f)) continue;

			auto const& tree = m_merkle_trees[f];

			// TODO: allocate m_hash_verified lazily when a hash conflist occurs?
			// would save memory in the common case of no hash failures
			m_hash_verified[f].resize(std::size_t(m_files.file_num_blocks(f)), all_verified);
			if (m_hash_verified[f].size() == 1)
			{
				// the root hash comes from the metadata so it is always verified
				TORRENT_ASSERT(!tree.root().is_all_zeros());
				m_hash_verified[f][0] = true;
			}

			if (m_files.file_size(f) <= m_files.piece_length())
				continue;

			m_piece_hash_requested[f].resize((m_files.file_num_pieces(f) + 511) / 512);

			int const piece_layer_idx = merkle_num_layers(
				merkle_num_leafs(m_files.file_num_blocks(f))) - m_piece_layer;
			int const piece_layer_start = merkle_layer_start(piece_layer_idx);

			// check for hashes we already have and flag entries in m_piece_hash_requested
			// so that we don't request them again
			for (int i = 0; i < int(m_piece_hash_requested[f].size()); ++i)
			{
				for (int j = i * 512;; ++j)
				{
					if (j == (i + 1) * 512 || j >= m_files.file_num_pieces(f))
					{
						m_piece_hash_requested[f][i].have = true;
						break;
					}
					if ((m_files.piece_length() == default_block_size && !m_hash_verified[f][std::size_t(j)])
						|| (m_files.piece_length() > default_block_size
							&& !tree.has_node(piece_layer_start + j)))
						break;
				}
			}
		}
	}

	hash_request hash_picker::pick_hashes(typed_bitfield<piece_index_t> const& pieces)
	{
		auto const now = aux::time_now();

		// this is for a future per-block request feature
#if 0
		if (!m_priority_block_requests.empty())
		{
			auto& req = m_priority_block_requests.front();
			node_index const nidx(req.file, m_files.file_first_block_node(req.file) + req.block);
			hash_request hash_req(req.file
				, 0
				, req.block
				, 2
				, layers_to_verify(nidx) + 1);
			req.num_requests++;
			std::sort(m_priority_block_requests.begin(), m_priority_block_requests.end());
			return hash_req;
		}
#endif

		if (!m_piece_block_requests.empty())
		{
			auto const req = std::find_if(m_piece_block_requests.begin(), m_piece_block_requests.end()
				, [now](piece_block_request const& e)
					{ return e.last_request == min_time() || e.last_request - now > min_request_interval; });
			if (req != m_piece_block_requests.end())
			{
				int const blocks_per_piece = m_files.piece_length() / default_block_size;

				// number of blocks from the start of the file
				int const first_block = static_cast<int>(req->piece) * blocks_per_piece;
				node_index const nidx(req->file, m_files.file_first_block_node(req->file) + first_block);
				hash_request hash_req(req->file
					, 0
					, first_block
					, blocks_per_piece
					, layers_to_verify(nidx) + merkle_num_layers(blocks_per_piece));
				req->num_requests++;
				req->last_request = now;
				std::sort(m_piece_block_requests.begin(), m_piece_block_requests.end());
				return hash_req;
			}
		}

		for (file_index_t fidx(0); fidx < m_piece_hash_requested.end_index(); ++fidx)
		{
			if (m_files.pad_file_at(fidx)) continue;

			int const file_first_piece = int(m_files.file_offset(fidx) / m_files.piece_length());
			int const num_layers = file_num_layers(fidx);
			int const piece_tree_root_layer = std::max(0, num_layers - m_piece_tree_root_layer);
			int const piece_tree_root_start = merkle_layer_start(piece_tree_root_layer);

			int i = -1;
			for (auto& r : m_piece_hash_requested[fidx])
			{
				++i;
				if (r.have ||
					(r.last_request != min_time()
					 && now - r.last_request < min_request_interval))
				{
					continue;
				}

				bool have = false;
				for (int p = i * 512; p < std::min<int>((i + 1) * 512, m_files.file_num_pieces(fidx)); ++p)
				{
					if (pieces[piece_index_t{file_first_piece + p}])
					{
						have = true;
						break;
					}
				}

				if (!have) continue;

				int const piece_tree_root = piece_tree_root_start + i;

				++r.num_requests;
				r.last_request = now;

				int const piece_tree_num_layers
					= num_layers - piece_tree_root_layer - m_piece_layer;

				return hash_request(fidx
					, m_piece_layer
					, i * 512
					, std::min(512, merkle_num_leafs(int(m_files.file_num_pieces(fidx) - i * 512)))
					, layers_to_verify({ fidx, piece_tree_root }) + piece_tree_num_layers);
			}
		}

		return {};
	}

	add_hashes_result hash_picker::add_hashes(hash_request const& req, span<sha256_hash const> hashes)
	{
		TORRENT_ASSERT(validate_hash_request(req, m_files));

		int const unpadded_count = std::min(req.count, m_files.file_num_pieces(req.file) - req.index);
		int const leaf_count = merkle_num_leafs(req.count);
		int const base_num_layers = merkle_num_layers(leaf_count);
		int const num_uncle_hashes = std::max(0, req.proof_layers - base_num_layers + 1);

		if (req.count + num_uncle_hashes != hashes.size())
			return add_hashes_result(false);

		// for now we rely on only requesting piece hashes in 512 chunks
		if (req.base == m_piece_layer
			&& req.count != 512
			&& (req.count > 512 || unpadded_count != m_files.file_num_pieces(req.file) - req.index))
			return add_hashes_result(false);

		// for now we only support receiving hashes at the piece and leaf layers
		if (req.base != m_piece_layer && req.base != 0)
			return add_hashes_result(false);

		// the incoming list of hashes is really two separate lists, the lowest
		// layer of hashes we requested (typically the block- or piece layer).
		// There are req.count of those, then there are the uncle hashes
		// required to prove the correctness of the first hashes, to anchor the
		// new hashes in the existing tree
		auto const uncle_hashes = hashes.subspan(req.count);
		hashes = hashes.first(req.count);

		TORRENT_ASSERT(uncle_hashes.size() == num_uncle_hashes);

		aux::vector<sha256_hash> tree(merkle_num_nodes(leaf_count));
		std::copy(hashes.begin(), hashes.end(), tree.end() - leaf_count);

		// the end of a file is a special case, we may need to pad the leaf layer
		if (req.base == m_piece_layer && leaf_count != req.count)
		{
			sha256_hash const pad_hash = merkle_pad(
				m_files.piece_length() / default_block_size, 1);
			for (int i = req.count; i < leaf_count; ++i)
				tree[tree.end_index() - leaf_count + i] = pad_hash;
		}

		merkle_fill_tree(tree, leaf_count);

		int const base_layer_idx = file_num_layers(req.file) - req.base;

		if (base_layer_idx <= 0)
			return add_hashes_result(false);

		// TODO: use strucutured bindings here in C++17
		aux::vector<std::pair<sha256_hash, sha256_hash>> proofs;
		sha256_hash tree_root;
		std::tie(proofs, tree_root) = merkle_check_proofs(
			tree[0], uncle_hashes, req.index >> base_num_layers);

		int const total_add_layers = std::max(req.proof_layers + 1, base_num_layers);
		int const root_layer_offset = req.index >> total_add_layers;
		int const proof_root_idx = merkle_to_flat_index(base_layer_idx - total_add_layers
			, root_layer_offset);
		auto& dst_tree = m_merkle_trees[req.file];

		if (!dst_tree.compare_node(proof_root_idx, tree_root))
			return add_hashes_result(false);

		add_hashes_result ret(true);

		int const dest_start_idx = merkle_to_flat_index(base_layer_idx, req.index);
		ret.hash_failed = dst_tree.add_hashes(dest_start_idx, tree);
		dst_tree.add_proofs(dest_start_idx >> base_num_layers, proofs);

		if (req.base == 0)
		{
			std::fill_n(m_hash_verified[req.file].begin() + req.index, unpadded_count, true);
			// TODO: add passing pieces to ret.hash_passed
		}
		else
		{
			TORRENT_ASSERT(req.base == m_piece_layer);
			int const file_piece_offset = int(m_files.file_offset(req.file) / m_files.piece_length());

			ret.hash_passed = dst_tree.check_pieces(req.base, req.index
				, file_piece_offset, hashes);
		}

		return ret;
	}

	set_block_hash_result hash_picker::set_block_hash(piece_index_t const piece
		, int const offset, sha256_hash const& h)
	{
		TORRENT_ASSERT(offset >= 0);
		auto const f = m_files.file_index_at_piece(piece);
		auto& merkle_tree = m_merkle_trees[f];
		piece_index_t const file_first_piece = m_files.piece_index_at_file(f);
		std::int64_t const block_offset = static_cast<int>(piece) * std::int64_t(m_files.piece_length())
			+ offset - m_files.file_offset(f);
		int const block_index = aux::numeric_cast<int>(block_offset / default_block_size);
		int const first_block_index = m_files.file_first_block_node(f);
		int const block_tree_index = first_block_index + block_index;

		if (m_files.pad_file_at(f))
		{
			// TODO: verify pad file hashes
			return { 0, 0 };
		}

		// if this blocks's hash is already known, check the passed-in hash against it
		if (m_hash_verified[f][std::size_t(block_index)])
		{
			TORRENT_ASSERT(merkle_tree.has_node(block_tree_index));
			if (block_tree_index > 0)
				TORRENT_ASSERT(merkle_tree.has_node(merkle_get_parent(block_tree_index)));
			return merkle_tree[block_tree_index] == h ? set_block_hash_result{offset / default_block_size, 1}
				: set_block_hash_result::block_hash_failed();
		}
		else if (h.is_all_zeros())
		{
			TORRENT_ASSERT_FAIL();
			return set_block_hash_result::block_hash_failed();
		}

		// TODO: use structured bindings in C++17
		aux::merkle_tree::set_block_result result;
		int leafs_index;
		int leafs_size;
		std::tie(result, leafs_index, leafs_size) = merkle_tree.set_block(block_index, h);

		if (result == aux::merkle_tree::set_block_result::unknown)
			return set_block_hash_result::unknown();

		if (result == aux::merkle_tree::set_block_result::hash_failed)
		{
			// If the hash failure was detected within a single piece then report a piece failure
			// otherwise report unknown. The pieces will be checked once their hashes have been
			// downloaded.
			if (leafs_size <= m_files.piece_length() / default_block_size)
				return set_block_hash_result::piece_hash_failed();
			else
				return set_block_hash_result::unknown();
		}
		else
		{
			int const file_num_blocks = m_files.file_num_blocks(f);
			std::fill_n(m_hash_verified[f].begin() + leafs_index, std::min(leafs_size, file_num_blocks - leafs_index), true);
		}

		int const blocks_per_piece = m_files.piece_length() / default_block_size;
		return { int(leafs_index - static_cast<int>(piece - file_first_piece) * blocks_per_piece)
			, std::min(leafs_size, m_files.file_num_pieces(f) * blocks_per_piece - leafs_index) };
	}

	void hash_picker::hashes_rejected(hash_request const& req)
	{
		TORRENT_ASSERT(req.base == m_piece_layer && req.index % 512 == 0);

		for (int i = req.index; i < req.index + req.count; i += 512)
		{
			m_piece_hash_requested[req.file][i / 512].last_request = min_time();
			--m_piece_hash_requested[req.file][i / 512].num_requests;
		}

		// this is for a future per-block request feature
#if 0
		else if (req.base == 0)
		{
			priority_block_request block_req(req.file, req.index);
			auto const existing_req = std::find(
				m_priority_block_requests.begin()
				, m_priority_block_requests.end()
				, block_req);

			if (existing_req == m_priority_block_requests.end())
			{
				m_priority_block_requests.insert(m_priority_block_requests.begin()
					, priority_block_request(req.file, req.index));
			}
		}
#endif
	}

	void hash_picker::verify_block_hashes(piece_index_t const index)
	{
		file_index_t const fidx = m_files.file_index_at_piece(index);
		piece_index_t::diff_type const piece = index - m_files.piece_index_at_file(fidx);
		piece_block_request req(fidx, piece);

		if (std::find(m_piece_block_requests.begin(), m_piece_block_requests.end(), req)
			!= m_piece_block_requests.end())
			return; // already requested

		m_piece_block_requests.insert(m_piece_block_requests.begin(), req);
	}

	bool hash_picker::have_hash(piece_index_t const index) const
	{
		file_index_t const f = m_files.file_index_at_piece(index);
		if (m_files.file_size(f) <= m_files.piece_length()) return true;
		piece_index_t const file_first_piece(int(m_files.file_offset(f) / m_files.piece_length()));
		return m_merkle_trees[f].has_node(m_files.file_first_piece_node(f) + int(index - file_first_piece));
	}

	bool hash_picker::have_all(file_index_t const file) const
	{
		return std::find(m_hash_verified[file].begin(), m_hash_verified[file].end(), false) == m_hash_verified[file].end();
	}

	bool hash_picker::have_all() const
	{
		for (file_index_t f : m_files.file_range())
			if (!have_all(f)) return false;
		return true;
	}

	bool hash_picker::piece_verified(piece_index_t const piece) const
	{
		file_index_t const f = m_files.file_index_at_piece(piece);
		piece_index_t const file_first_piece(int(m_files.file_offset(f) / m_files.piece_length()));
		int const block_offset = static_cast<int>(piece - file_first_piece) * (m_files.piece_length() / default_block_size);
		int const blocks_in_piece = m_files.blocks_in_piece2(piece);
		auto const& file_leafs = m_hash_verified[f];
		return std::all_of(file_leafs.begin() + block_offset, file_leafs.begin() + block_offset + blocks_in_piece
			, [](bool b) { return b; });
	}

	int hash_picker::layers_to_verify(node_index idx) const
	{
		// the root layer doesn't have a sibling so it should never
		// be requested as a proof layer
		// return -1 to signal to the caller that no proof is required
		// even for the first layer it is trying to verify
		if (idx.node == 0) return -1;

		int layers = 0;
		int const file_internal_layers = merkle_num_layers(merkle_num_leafs(m_files.file_num_pieces(idx.file))) - 1;
		auto const& tree = m_merkle_trees[idx.file];

		for (;;)
		{
			idx.node = merkle_get_parent(idx.node);
			if (tree.has_node(idx.node)) break;
			layers++;
			if (layers == file_internal_layers) return layers;
		}

		return layers;
	}

	int hash_picker::file_num_layers(file_index_t const idx) const
	{
		return merkle_num_layers(merkle_num_leafs(m_files.file_num_blocks(idx)));
	}
}
