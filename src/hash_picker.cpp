/*

Copyright (c) 2017, BitTorrent Inc.
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

namespace libtorrent
{
	namespace
	{
		// used as a flag in m_piece_hash_requested to indicate that all of the hashes
		// for an entry where loaded from disk so we don't know where we originaly got them
		torrent_peer* const source_unknown = reinterpret_cast<torrent_peer*>(-1);

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

		std::pair<int,int> merkle_piece_range(int idx, int num_pieces)
		{
			int const piece_layer_size = merkle_num_leafs(num_pieces);
			int const first_piece = merkle_num_nodes(piece_layer_size) - piece_layer_size;

			if (idx < first_piece)
			{
				int count = 1;
				while (idx < first_piece)
				{
					idx = merkle_get_first_child(idx);
					count *= 2;
				}
				return { idx, count };
			}
			else
			{
				while (idx >= first_piece + piece_layer_size)
				{
					idx = merkle_get_parent(idx);
				}
				return { idx, 1 };
			}
		}
	}

	hash_picker::hash_picker(file_storage const& files
		, aux::vector<std::vector<sha256_hash>, file_index_t>& trees
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

			// TODO: allocate m_hash_verified lazily when a hash conflist occurs?
			// would save memory in the common case of no hash failures
			m_hash_verified[f].resize(m_files.file_num_blocks(f), all_verified);
			if (m_hash_verified[f].size() == 1)
			{
				// the root hash comes from the metadata so it is always verified
				TORRENT_ASSERT(!m_merkle_trees[f][0].is_all_zeros());
				m_hash_verified[f][0] = true;
			}

			if (m_files.file_size(f) > m_files.piece_length())
			{
				m_piece_hash_requested[f].resize((m_files.file_num_pieces(f) + 511) / 512, nullptr);
			}
			else
			{
				continue;
			}

			int const piece_layer_idx = merkle_num_layers(m_files.file_num_blocks(f)) - m_piece_layer;

			// check for hashes we already have and flag entries in m_piece_hash_requested
			// so that we don't request them again
			for (int i = 0; i < int(m_piece_hash_requested[f].size()); ++i)
			{
				for (int j = i * 512;; ++j)
				{
					if (j == (i + 1) * 512 || j >= m_files.file_num_pieces(f))
					{
						m_piece_hash_requested[f][i] = source_unknown;
						break;
					}
					if ((m_files.piece_length() == default_block_size && !m_hash_verified[f][j])
						|| (m_files.piece_length() > default_block_size
							&& m_merkle_trees[f][merkle_to_flat_index(piece_layer_idx, j)].is_all_zeros()))
						break;
				}
			}
		}
	}

	void hash_picker::set_verified(aux::vector<std::vector<bool>, file_index_t> const& verified)
	{
		if (verified.empty()) return;
		TORRENT_ASSERT(int(verified.size()) == m_files.num_files());
		for (file_index_t f(0); f != m_files.end_file(); ++f)
		{
			auto& v = m_hash_verified[f];
			auto& vi = verified[f];
			TORRENT_ASSERT(v.size() == vi.size());
			std::transform(v.begin(), v.end(), vi.begin(), v.begin(), [](bool v1, bool v2) { return v1 || v2; });
		}
	}

	std::vector<hash_request> hash_picker::pick_hashes(typed_bitfield<piece_index_t> const& pieces
		, int num_blocks, peer_connection_interface* peer)
	{
		std::vector<hash_request> ret;
		// convert from 16KB blocks to 32 byte hashes
		int num_hashes = num_blocks * 512;

		for (piece_index_t p : m_priority_pieces)
		{
			if (!pieces[p]) continue;

			int const fidx = m_files.file_index_at_offset(p * m_files.piece_length());
			int const piece_idx = (int(p) - m_files.file_offset(fidx) / m_files.piece_length()) / 512;
			int const piece_tree_root_layer = std::max(0, file_num_layers(fidx) - m_piece_tree_root_layer);

			int piece_tree_root = merkle_to_flat_index(piece_tree_root_layer, piece_idx);

			ret.emplace_back(fidx
				, m_piece_layer
				, piece_idx * 512
				, std::min(512, int(m_files.file_num_pieces(fidx) - piece_idx * 512))
				, layers_to_verify({ fidx, piece_tree_root }) + m_piece_tree_root_layer);

			m_piece_hash_requested[fidx][piece_idx] = peer->peer_info_struct();

			num_hashes -= ret.back().count;

			if (num_hashes <= 0)
				return ret;
		}

		for (chunks_request p : m_chunk_requests)
		{
			if (p.peer != nullptr) continue;
			if (!pieces[p.index]) continue;

			int const fidx = m_files.file_index_at_offset(p.index * m_files.piece_length());
			int const chunks_per_piece = m_files.piece_size2(p.index) / default_block_size;
			int const piece_idx = int(p.index) - m_files.file_offset(fidx) / m_files.piece_length();

			ret.emplace_back(fidx
				, 0
				, piece_idx * chunks_per_piece
				, std::max(chunks_per_piece, 2)
				, 0);

			p.peer = peer;

			num_hashes -= ret.back().count;

			if (num_hashes <= 0)
				return ret;
		}

		for (int fidx = 0; fidx < int(m_piece_hash_requested.size()); ++fidx)
		{
			int const file_first_piece = m_files.file_offset(fidx) / m_files.piece_length();
			int const piece_tree_root_layer = std::max(0, file_num_layers(fidx) - m_piece_tree_root_layer);
			auto& f = m_piece_hash_requested[fidx];

			for (int i = 0; i < int(f.size()); ++i)
			{
				if (f[i] != nullptr) continue;

				bool have = false;
				for (int p = i * 512; p < std::min<int>((i + 1) * 512, m_files.file_num_pieces(fidx)); ++p)
				{
					if (pieces[file_first_piece + p])
					{
						have = true;
						break;
					}
				}

				if (!have) continue;

				int piece_tree_root = merkle_to_flat_index(piece_tree_root_layer, i);

				ret.emplace_back(fidx
					, m_piece_layer
					, i * 512
					, std::min(512, int(m_files.file_num_pieces(fidx) - i * 512))
					, layers_to_verify({ fidx, piece_tree_root }) + m_piece_tree_root_layer - m_piece_layer);

				f[i] = peer->peer_info_struct();

				num_hashes -= ret.back().count;

				if (num_hashes <= 0)
					return ret;
			}
		}

		return ret;
	}

	add_hashes_result hash_picker::add_hashes(hash_request const& req, span<sha256_hash> hashes)
	{
		// limit the size of the base layer to something reasonable
		// Chunks are requested for an entire piece so this limit
		// effectivly caps the piece size we can handle. A limit of 8192
		// corresponds to a piece size of 128MB.
		if (req.count > 8192)
			return add_hashes_result(false);

		int const num_layers = file_num_layers(req.file);

		if (req.base >= num_layers || req.proof_layers >= num_layers)
			return add_hashes_result(false);

		if (req.index + req.count > (1 << (num_layers - req.base)))
			return add_hashes_result(false);

		int const count = merkle_num_leafs(req.count);
		int const base_num_layers = merkle_num_layers(count);

		if (req.count + std::max(0, req.proof_layers - (base_num_layers - 1)) != int(hashes.size()))
			return add_hashes_result(false);

		// for now we rely on only requesting piece hashes in 512 chunks
		if (req.base == m_piece_layer
			&& req.count != 512
			&& req.count != m_files.file_num_pieces(req.file) - req.index)
			return add_hashes_result(false);

		// for now we only support receiving hashes at the piece and leaf layers
		if (req.base != m_piece_layer && req.base != 0)
			return add_hashes_result(false);

		std::vector<sha256_hash> tree(merkle_num_nodes(count));
		std::copy(hashes.begin(), hashes.begin() + req.count, tree.end() - count);

		// the end of a file is a special case, we may need to pad the leaf layer
		if (req.base == m_piece_layer && count != req.count)
		{
			sha256_hash pad_hash = merkle_root(std::vector<sha256_hash>(m_files.piece_length() / default_block_size));
			for (int i = req.count; i < count; ++i)
				tree[tree.size() - count + i] = pad_hash;
		}

		merkle_fill_tree(tree, count);

		int const base_layer_idx = num_layers - req.base;

		if (base_layer_idx <= 0)
			return add_hashes_result(false);

		int proof_leafs = req.count;
		std::vector<std::pair<sha256_hash, sha256_hash>> proofs(
			std::max(0, req.proof_layers - base_num_layers + 1));
		auto proof_iter = proofs.begin();
		sha256_hash tree_root = tree[0];
		for (auto proof = hashes.begin() + req.count; proof != hashes.end(); ++proof)
		{
			proof_leafs *= 2;
			bool proof_right = req.index % proof_leafs < proof_leafs / 2;
			if (proof_right)
			{
				proof_iter->first = tree_root;
				proof_iter->second = *proof;
			}
			else
			{
				proof_iter->first = *proof;
				proof_iter->second = tree_root;
			}
			hasher256 h;
			h.update(proof_iter->first);
			h.update(proof_iter->second);
			tree_root = h.final();
			++proof_iter;
		}

		int const total_add_layers = std::max(req.proof_layers + 1, base_num_layers);
		int const root_layer_offset = req.index / (1 << total_add_layers);
		int const proof_root_idx = merkle_to_flat_index(base_layer_idx - total_add_layers
			, root_layer_offset);
		if (m_merkle_trees[req.file][proof_root_idx] != tree_root)
		{
			return add_hashes_result(false);
		}

		// The proof has been verrified, copy the hashes into the file's merkle tree

		// first fill in the subtree of known hashes from the base layer
		int dest_start_idx = merkle_to_flat_index(base_layer_idx, req.index);
		int source_start_idx = int(tree.size()) - count;

		add_hashes_result ret(true);

		for (int count = req.count; count != 0; count /= 2)
		{
			for (int i = 0; i < count; ++i)
			{
				if (!m_merkle_trees[req.file][dest_start_idx + i].is_all_zeros()
					&& m_merkle_trees[req.file][dest_start_idx + i] != tree[source_start_idx + i])
				{
					// this must be a chunk hash because inner nodes are not filled in until
					// they can be verified
					int const file_first_leaf = m_files.file_first_block_node(req.file);
					TORRENT_ASSERT(dest_start_idx + i >= file_first_leaf);
					piece_index_t piece = (dest_start_idx + i - file_first_leaf) / (m_files.piece_length() / default_block_size);
					int block_index = (dest_start_idx + i - file_first_leaf) % (m_files.piece_length() / default_block_size);
					ret.hash_failed[piece].push_back(block_index);
				}

				m_merkle_trees[req.file][dest_start_idx + i] = tree[source_start_idx + i];
			}
			if (count == 1) break;
			dest_start_idx = merkle_get_parent(dest_start_idx);
			source_start_idx = merkle_get_parent(source_start_idx);
		}

		// now copy the string of proof hashes
		for (auto proof : proofs)
		{
			if (dest_start_idx & 1)
			{
				m_merkle_trees[req.file][dest_start_idx] = proof.first;
				m_merkle_trees[req.file][dest_start_idx + 1] = proof.second;
			}
			else
			{
				m_merkle_trees[req.file][dest_start_idx - 1] = proof.first;
				m_merkle_trees[req.file][dest_start_idx] = proof.second;
			}
			dest_start_idx = merkle_get_parent(dest_start_idx);
		}

		if (req.base == 0)
		{
			std::fill_n(m_hash_verified[req.file].begin() + req.index, req.count, true);

			if (base_num_layers == m_piece_layer)
			{
				// this was a chunk hash request, remove the downloading record
				piece_index_t piece = m_files.file_offset(req.file) / m_files.piece_length() + root_layer_offset;
				auto new_end = std::remove_if(m_chunk_requests.begin(), m_chunk_requests.end()
					, [piece](chunks_request const& e) { return e.index == piece; });
				m_chunk_requests.erase(new_end, m_chunk_requests.end());
			}
			// TODO: add passing pieces to ret.hash_passed
		}

		if (req.base != 0)
		{
			TORRENT_ASSERT(req.base == m_piece_layer);
			int const file_num_chunks = m_files.file_num_blocks(req.file);
			int const file_first_leaf = m_files.file_first_block_node(req.file);
			int const file_first_piece = m_files.file_first_piece_node(req.file);
			int const base_layer_index = merkle_num_layers(file_num_chunks) - req.base;

			// it may now be possible to verify the hashes of previously received chunks
			// try to verify as many child nodes of the received hashes as possible
			for (int i = 0; i < req.count; ++i)
			{
				int const piece = req.index + i;
				if (!m_merkle_trees[req.file][merkle_get_first_child(file_first_piece + piece)].is_all_zeros()
					&& !m_merkle_trees[req.file][merkle_get_first_child(file_first_piece + piece) + 1].is_all_zeros())
				{
					// this piece is already verified
					continue;
				}

				int const first_leaf = piece << req.base;
				int const num_leafs = 1 << req.base;

				bool done = false;
				for (int j = 0; j < std::min(num_leafs, file_num_chunks - first_leaf); ++j)
				{
					if (m_merkle_trees[req.file][file_first_leaf + first_leaf + j].is_all_zeros())
					{
						done = true;
						break;
					}
				}
				if (done) continue;

				merkle_fill_tree(m_merkle_trees[req.file], num_leafs, file_first_leaf + first_leaf);
				if (m_merkle_trees[req.file][merkle_to_flat_index(base_layer_index, req.index + i)] != hashes[i])
				{
					merkle_clear_tree(m_merkle_trees[req.file], num_leafs / 2, merkle_get_parent(file_first_leaf + first_leaf));
					m_merkle_trees[req.file][merkle_to_flat_index(base_layer_index, req.index + i)] = hashes[i];
					TORRENT_ASSERT(num_leafs == m_files.piece_length() / default_block_size);
					verify_chunk_hashes(m_files.file_offset(req.file) / m_files.piece_length() + req.index);
				}
				else
				{
					ret.hash_passed.push_back(piece);
				}
			}
		}

		return ret;
	}

	set_chunk_hash_result hash_picker::set_chunk_hash(piece_index_t piece, int offset, sha256_hash const& h)
	{
		auto f = m_files.file_index_at_offset(piece * m_files.piece_length());
		auto& merkle_tree = m_merkle_trees[f];
		int const chunk_offset = piece * m_files.piece_length() + offset - m_files.file_offset(f);
		int const chunk_index = chunk_offset / default_block_size;
		int const file_num_chunks = m_files.file_num_blocks(f);
		int const first_chunk_index = m_files.file_first_block_node(f);
		int const chunk_tree_index = first_chunk_index + chunk_index;

		if (m_files.pad_file_at(f))
		{
			// TODO: verify pad file hashes
			return set_chunk_hash_result::success;
		}

		// if this chunk's hash is already known, check the passed-in hash against it
		if (m_hash_verified[f][chunk_index])
		{
			TORRENT_ASSERT(!merkle_tree[chunk_tree_index].is_all_zeros());
			if (chunk_tree_index > 0)
				TORRENT_ASSERT(!merkle_tree[merkle_get_parent(chunk_tree_index)].is_all_zeros());
			return merkle_tree[chunk_tree_index] == h ? set_chunk_hash_result{offset / default_block_size, 1}
				: set_chunk_hash_result::chunk_hash_failed;
		}
		else if (h.is_all_zeros())
		{
			TORRENT_ASSERT_FAIL();
			return set_chunk_hash_result::chunk_hash_failed;
		}

		merkle_tree[chunk_tree_index] = h;

		// to avoid wasting a lot of time hashing nodes only to discover they
		// cannot be verrified, check first to see if the root of the largest
		// computable subtree is known

		// find the largest block of leafs from a single subtree we know the hashes of
		int leafs_index = chunk_index;
		int leafs_size = 1;
		int root_index = merkle_get_sibling(first_chunk_index + chunk_index);
		for (int i = chunk_index;; i >>= 1)
		{
			int const first_check_index = leafs_index + ((i & 1) ? -leafs_size : leafs_size);
			bool done = false;
			for (int j = 0; j < std::min(leafs_size, file_num_chunks - first_check_index); ++j)
			{
				if (merkle_tree[first_chunk_index + first_check_index + j].is_all_zeros())
				{
					done = true;
					break;
				}
			}
			if (done) break;
			if (i & 1) leafs_index -= leafs_size;
			leafs_size *= 2;
			root_index = merkle_get_parent(root_index);
			// if an inner node is known then its parent must be known too
			// so if the root is known the next sibling subtree should already
			// be computed if all of its leafs have valid hashes
			if (!merkle_tree[root_index].is_all_zeros()) break;
			TORRENT_ASSERT(root_index != 0);
			TORRENT_ASSERT(leafs_index >= 0);
			TORRENT_ASSERT(leafs_size <= merkle_num_leafs(file_num_chunks));
		}

		TORRENT_ASSERT(leafs_index >= 0);
		TORRENT_ASSERT(leafs_index < merkle_num_leafs(m_files.file_num_blocks(f)));
		TORRENT_ASSERT(leafs_index + leafs_size > chunk_index);

		// if the root node is unknown the hashes cannot be verified yet
		if (merkle_tree[root_index].is_all_zeros())
			return set_chunk_hash_result::unknown;

		// save the root hash because merkle_fill_tree will overwrite it
		sha256_hash root = merkle_tree[root_index];
		merkle_fill_tree(merkle_tree, leafs_size, first_chunk_index + leafs_index);

		if (root != merkle_tree[root_index])
		{
			// hash failure, clear all the internal nodes
			merkle_clear_tree(merkle_tree, leafs_size / 2, merkle_get_parent(first_chunk_index + leafs_index));
			merkle_tree[root_index] = root;
			// If the hash failure was detected within a single piece then report a piece failure
			// otherwise report unknown. The pieces will be checked once their hashes have been
			// downloaded.
			if (leafs_size <= m_files.piece_length() / default_block_size)
				return set_chunk_hash_result::piece_hash_failed;
			else
				return set_chunk_hash_result::unknown;
		}
		else
		{
			std::fill_n(m_hash_verified[f].begin() + leafs_index, std::min(leafs_size, file_num_chunks - leafs_index), true);
		}

		int const blocks_per_piece = m_files.piece_length() / default_block_size;
		piece_index_t const file_first_piece = m_files.file_offset(f) / m_files.piece_length();
		return { leafs_index - (piece - file_first_piece) * blocks_per_piece
			, std::min(leafs_size, m_files.file_num_pieces(f) * (m_files.piece_length() / default_block_size) - leafs_index) };
	}

	void hash_picker::hashes_rejected(peer_connection_interface* source, hash_request const& req)
	{
		if (req.base == m_piece_layer && req.index % 512 == 0)
		{
			for (int i = req.index / 512; i < req.index / 512 + req.count / 512; ++i)
			{
				if (m_piece_hash_requested[req.file][i] == source->peer_info_struct())
					m_piece_hash_requested[req.file][i] = nullptr;
			}
		}
		else if (req.base == 0)
		{
			for (auto& r : m_chunk_requests)
			{
				auto const piece_offset = r.index * m_files.piece_length();
				auto const f = m_files.file_index_at_offset(piece_offset);
				if (f != req.file) continue;
				auto const file_offset = m_files.file_offset(f);
				auto const piece_offset_blocks = (piece_offset - file_offset) / default_block_size;
				TORRENT_ASSERT((piece_offset - file_offset) % default_block_size == 0);
				if (req.index != piece_offset_blocks) continue;
				if (r.peer == source)
					r.peer = nullptr;
				break;
			}
		}
	}

	void hash_picker::peer_disconnected(peer_connection_interface* peer)
	{
		torrent_peer* tp = peer->peer_info_struct();
		for (int f = 0; f < int(m_piece_hash_requested.size()); ++f)
		{
			for (int i = 0; i < int(m_piece_hash_requested[f].size()); ++i)
			{
				if (m_piece_hash_requested[f][i] == tp)
				{
					if (std::any_of(m_merkle_trees[f].begin() + merkle_to_flat_index(m_piece_layer, i * 512)
						, m_merkle_trees[f].begin() + merkle_to_flat_index(m_piece_layer, std::min(int(m_files.file_num_pieces(f)), (i + 1) * 512))
						, [](sha256_hash const& e) { return e.is_all_zeros(); }))
					{
						m_piece_hash_requested[f][i] = nullptr;
					}
				}
			}
		}

		for (auto& r : m_chunk_requests)
		{
			if (r.peer == peer) r.peer = nullptr;
		}
	}

	void hash_picker::verify_chunk_hashes(piece_index_t index)
	{
		if (std::find_if(m_chunk_requests.begin(), m_chunk_requests.end()
			, [index](chunks_request const& r) { return r.index == index; })
			!= m_chunk_requests.end())
			return; // already requested
		m_chunk_requests.push_back(chunks_request{ index });
	}

	bool hash_picker::have_hash(piece_index_t index) const
	{
		file_index_t f = m_files.file_index_at_offset(index * m_files.piece_length());
		if (m_files.file_size(f) <= m_files.piece_length()) return true;
		piece_index_t const file_first_piece(int(m_files.file_offset(f) / m_files.piece_length()));
		return !m_merkle_trees[f][m_files.file_first_piece_node(f) + int(index - file_first_piece)].is_all_zeros();
	}

	bool hash_picker::have_all(file_index_t file) const
	{
		return std::find(m_hash_verified[file].begin(), m_hash_verified[file].end(), false) == m_hash_verified[file].end();
	}

	bool hash_picker::have_all() const
	{
		for (file_index_t f(0); f != m_files.end_file(); ++f)
			if (!have_all(f)) return false;
		return true;
	}

	bool hash_picker::piece_verified(piece_index_t piece) const
	{
		file_index_t f = m_files.file_index_at_offset(piece * m_files.piece_length());
		piece_index_t file_first_piece(int(m_files.file_offset(f) / m_files.piece_length()));;
		int const block_offset = (piece - file_first_piece) * (m_files.piece_length() / default_block_size);
		int const blocks_in_piece = (m_files.piece_size2(piece) + default_block_size - 1) / default_block_size;
		auto const& file_leafs = m_hash_verified[f];
		return std::all_of(file_leafs.begin() + block_offset, file_leafs.begin() + block_offset + blocks_in_piece
			, [](bool b) { return b; });
	}

	int hash_picker::layers_to_verify(node_index idx) const
	{
		if (idx.node == 0) return 0;

		int layers = 0;
		int const file_internal_layers = merkle_num_layers(merkle_num_leafs(m_files.file_num_pieces(idx.file))) - 1;

		for (;;)
		{
			idx.node = merkle_get_parent(idx.node);
			if (!m_merkle_trees[idx.file][idx.node].is_all_zeros()) break;
			layers++;
			if (layers == file_internal_layers) return layers;
		}

		return layers;
	}

	int hash_picker::file_num_layers(file_index_t idx) const
	{
		return merkle_num_layers(merkle_num_leafs(m_files.file_num_blocks(idx)));
	}
}
