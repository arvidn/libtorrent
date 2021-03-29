/*

Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/ffs.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace lt::aux {

	merkle_tree::merkle_tree(int const num_blocks, int const blocks_per_piece, char const* r)
		: m_root(r)
		, m_num_blocks(num_blocks)
		, m_blocks_per_piece_log(numeric_cast<std::uint8_t>(
			log2p1(numeric_cast<std::uint32_t>(blocks_per_piece))))
		, m_mode(mode_t::empty_tree)
	{
		// blocks per piece must be an even power of 2
		TORRENT_ASSERT(((blocks_per_piece - 1) & blocks_per_piece) == 0);
		TORRENT_ASSERT(m_root != nullptr);
		TORRENT_ASSERT(this->blocks_per_piece() == blocks_per_piece);
	}

	sha256_hash merkle_tree::root() const { return sha256_hash(m_root); }

	void merkle_tree::load_tree(span<sha256_hash const> t)
	{
		if (t.empty()) return;
		if (root() != t[0]) return;
		if (size() != static_cast<std::size_t>(t.size())) return;

		allocate_full();

		merkle_validate_copy(t, m_tree, root());

		optimize_storage();
		optimize_storage_piece_layer();
	}

	void merkle_tree::clear()
	{
		m_tree.clear();
		m_tree.shrink_to_fit();
		m_mode = mode_t::empty_tree;
	}

namespace {

	// TODO: in C++20, use std::identity
	struct identity
	{
		bool operator()(bool b) const { return b; }
	};
}

	void merkle_tree::load_sparse_tree(span<sha256_hash const> t, std::vector<bool> const& mask)
	{
		TORRENT_ASSERT(mask.size() == size());
		if (size() != mask.size()) return;

		int const first_block = block_layer_start();
		int const end_block = first_block + m_num_blocks;

		TORRENT_ASSERT(first_block < int(mask.size()));
		TORRENT_ASSERT(end_block <= int(mask.size()));

		// if the mask covers all blocks, go straight to block_layer
		// mode, and validate
		if (std::all_of(mask.begin() + first_block, mask.begin() + end_block, identity()))
		{
			// the index in t that points to first_block
			auto const block_index = std::count_if(mask.begin(), mask.begin() + first_block, identity());

			// discrepancy
			if (t.size() < block_index + m_num_blocks)
				return clear();

			m_tree.assign(t.begin() + block_index, t.begin() + block_index + m_num_blocks);
			m_mode = mode_t::block_layer;

			sha256_hash const r = merkle_root(m_tree);
			// validation failed!
			if (r != root()) clear();
			return;
		}

		// if the piece layer is the same as the block layer, skip this next
		// check
		if (m_blocks_per_piece_log > 0)
		{
			int const first_piece = piece_layer_start();
			int const piece_count = num_pieces();
			int const end_piece = first_piece + piece_count;

			TORRENT_ASSERT(first_piece < int(mask.size()));
			TORRENT_ASSERT(end_piece <= int(mask.size()));

			// if the mask convers all pieces, and nothing below that layer, go
			// straight to piece_layer mode and validate
			if (std::all_of(mask.begin() + first_piece, mask.begin() + end_piece, identity())

				&& std::all_of(mask.begin() + end_piece, mask.end(), std::logical_not<>()))
			{
				// the index in t that points to first_piece
				auto const piece_index = std::count_if(mask.begin(), mask.begin() + first_piece, identity());
				// discrepancy
				if (t.size() < piece_index + piece_count)
					return clear();

				m_tree.assign(t.begin() + piece_index, t.begin() + piece_index + piece_count);
				m_mode = mode_t::piece_layer;

				sha256_hash const piece_layer_pad = merkle_pad(1 << m_blocks_per_piece_log, 1);
				sha256_hash const r = merkle_root(m_tree, piece_layer_pad);
				// validation failed!
				if (r != root()) clear();
				return;
			}
		}

		// if the mask has only zeros, go straight to empty tree mode
		if (t.empty() || std::none_of(mask.begin(), mask.end(), identity()))
			return clear();

		allocate_full();
		int cursor = 0;
		for (std::size_t i = 0, end = mask.size(); i < end; ++i)
		{
			if (!mask[i]) continue;
			if (cursor >= t.size()) break;
			m_tree[int(i)] = t[cursor++];
		}
		merkle_fill_partial_tree(m_tree);

		// this suggests that none of the hashes in the tree can be
		// validated against the root. We effectively have an empty tree.
		if (m_tree[0] != root())
			return clear();

		optimize_storage();
	}

	aux::vector<sha256_hash> merkle_tree::get_piece_layer() const
	{
		aux::vector<sha256_hash> ret;

		switch (m_mode)
		{
			case mode_t::uninitialized_tree: break;
			case mode_t::empty_tree: break;
			case mode_t::full_tree:
				ret.assign(m_tree.begin() + piece_layer_start()
					, m_tree.begin() + piece_layer_start() + num_pieces());
				break;
			case mode_t::piece_layer:
			{
				ret = m_tree;
				break;
			}
			case mode_t::block_layer:
			{
				ret.reserve(num_pieces());
				std::vector<sha256_hash> scratch_space;

				int const blocks_in_piece = blocks_per_piece();
				for (int b = 0; b < int(m_tree.size()); b += blocks_in_piece)
				{
					auto const leafs = span<sha256_hash const>(m_tree).subspan(b);
					ret.push_back(merkle_root_scratch(leafs, blocks_in_piece, sha256_hash{}, scratch_space));
				}
				break;
			}
		}
		return ret;
	}

	// returns false if the piece layer fails to validate against the root hash
	bool merkle_tree::load_piece_layer(span<char const> piece_layer)
	{
		if (m_mode == mode_t::block_layer) return true;

		int const npieces = num_pieces();
		if (piece_layer.size() != npieces * sha256_hash::size()) return false;

		int const piece_layer_size = merkle_num_leafs(npieces);
		auto const num_leafs = merkle_num_leafs(m_num_blocks);

		sha256_hash const pad_hash = merkle_pad(num_leafs, piece_layer_size);

		aux::vector<sha256_hash> pieces(npieces);
		for (int n = 0; n < npieces; ++n)
			pieces[n].assign(piece_layer.data() + n * sha256_hash::size());

		if (merkle_root(pieces, pad_hash) != root())
			return false;

		// if there's only 1 block per piece, the piece layer is the same as the
		// block layer, record that so we know there's no more work to do for
		// this file
		m_mode = m_blocks_per_piece_log == 0 ? mode_t::block_layer : mode_t::piece_layer;
		m_tree = std::move(pieces);

		return true;
	}

	std::map<piece_index_t, std::vector<int>> merkle_tree::add_hashes(
		int dest_start_idx, span<sha256_hash const> tree)
	{
		std::map<piece_index_t, std::vector<int>> failed_blocks;

		// we already have all hashes
		if (m_mode == mode_t::block_layer) return failed_blocks;

		allocate_full();

		// first fill in the subtree of known hashes from the base layer

		auto const num_leafs = merkle_num_leafs(m_num_blocks);
		auto const first_leaf = merkle_first_leaf(num_leafs);

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

					int const pos = dst_idx - first_leaf;
					int const piece = pos >> m_blocks_per_piece_log;
					int const block = pos & ((1 << m_blocks_per_piece_log) - 1);
					failed_blocks[piece_index_t{piece}].push_back(block);
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
		TORRENT_ASSERT(merkle_validate_proofs(dest_start_idx, proofs));

		allocate_full();

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
		, int const index, int const file_piece_offset
		, span<sha256_hash const> hashes)
	{
		std::vector<piece_index_t> passed_pieces;

		allocate_full();

		int const count = static_cast<int>(hashes.size());
		auto const file_num_leafs = merkle_num_leafs(m_num_blocks);
		auto const file_first_leaf = merkle_first_leaf(file_num_leafs);
		int const first_piece = file_first_leaf >> m_blocks_per_piece_log;

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
			if (first_leaf >= m_num_blocks) break;
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
				TORRENT_ASSERT(num_leafs == blocks_per_piece());
				//verify_block_hashes(m_files.file_offset(req.file) / m_files.piece_length() + index);
				// TODO: add to failed hashes
			}
			else
			{
				passed_pieces.push_back(piece_index_t{file_piece_offset + piece});
			}
		}

		optimize_storage();

		return passed_pieces;
	}

	std::tuple<merkle_tree::set_block_result, int, int> merkle_tree::set_block(int const block_index
		, sha256_hash const& h)
	{
		auto const num_leafs = merkle_num_leafs(m_num_blocks);
		auto const file_first_leaf = merkle_first_leaf(num_leafs);
		auto const block_tree_index = file_first_leaf + block_index;

		// TODO: add a special case for m_mode == mode_t::block_layer

		allocate_full();

		m_tree[block_tree_index] = h;

		// to avoid wasting a lot of time hashing nodes only to discover they
		// cannot be verified, check first to see if the root of the largest
		// computable subtree is known

		auto const [leafs_start, leafs_size, root_index] =
			merkle_find_known_subtree(m_tree, block_index, m_num_blocks);

		// if the root node is unknown the hashes cannot be verified yet
		if (m_tree[root_index].is_all_zeros())
			return std::make_tuple(set_block_result::unknown, leafs_start, leafs_size);

		// save the root hash because merkle_fill_tree will overwrite it
		sha256_hash const root = m_tree[root_index];
		merkle_fill_tree(m_tree, leafs_size, file_first_leaf + leafs_start);

		if (root != m_tree[root_index])
		{
			// hash failure, clear all the internal nodes
			merkle_clear_tree(m_tree, leafs_size / 2, merkle_get_parent(file_first_leaf + leafs_start));
			m_tree[root_index] = root;
			return std::make_tuple(set_block_result::hash_failed, leafs_start, leafs_size);
		}

		// attempting to optimize storage is quite costly, only do it if we have
		// a reason to believe it might have an effect
		if (block_index == m_num_blocks - 1 || !m_tree[block_tree_index + 1].is_all_zeros())
			optimize_storage();

		return std::make_tuple(set_block_result::ok, leafs_start, leafs_size);
	}

	std::size_t merkle_tree::size() const
	{
		return static_cast<std::size_t>(merkle_num_nodes(merkle_num_leafs(m_num_blocks)));
	}

	int merkle_tree::num_pieces() const
	{
		int const ps = blocks_per_piece();
		TORRENT_ASSERT(ps > 0);
		return (m_num_blocks + ps - 1) >> m_blocks_per_piece_log;
	}

	int merkle_tree::block_layer_start() const
	{
		int const num_leafs = merkle_num_leafs(m_num_blocks);
		TORRENT_ASSERT(num_leafs > 0);
		return merkle_first_leaf(num_leafs);
	}

	int merkle_tree::piece_layer_start() const
	{
		int const piece_layer_size = merkle_num_leafs(num_pieces());
		TORRENT_ASSERT(piece_layer_size > 0);
		return merkle_first_leaf(piece_layer_size);
	}

	int merkle_tree::num_leafs() const
	{
		return merkle_num_leafs(m_num_blocks);
	}

	bool merkle_tree::has_node(int const idx) const
	{
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				TORRENT_ASSERT_FAIL();
				return false;
			case mode_t::empty_tree: return idx == 0;
			case mode_t::full_tree: return !m_tree[idx].is_all_zeros();
			case mode_t::piece_layer: return idx < merkle_get_first_child(piece_layer_start());
			case mode_t::block_layer: return idx < block_layer_start() + m_num_blocks;
		}
		TORRENT_ASSERT_FAIL();
		return false;
	}

	bool merkle_tree::compare_node(int const idx, sha256_hash const& h) const
	{
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				TORRENT_ASSERT_FAIL();
				return h.is_all_zeros();
			case mode_t::empty_tree: return idx == 0 ? root() == h : h.is_all_zeros();
			case mode_t::full_tree: return m_tree[idx] == h;
			case mode_t::piece_layer: return idx < merkle_get_first_child(piece_layer_start());
			case mode_t::block_layer: return true;
		}
		TORRENT_ASSERT_FAIL();
		return false;
	}

	sha256_hash merkle_tree::operator[](int const idx) const
	{
		std::vector<sha256_hash> scratch;
		return get_impl(idx, scratch);
	}

	sha256_hash merkle_tree::get_impl(int idx, std::vector<sha256_hash>& scratch_space) const
	{
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				TORRENT_ASSERT_FAIL();
				return sha256_hash{};
			case mode_t::empty_tree: return idx == 0 ? root() : sha256_hash{};
			case mode_t::full_tree: return m_tree[idx];
			case mode_t::piece_layer:
			case mode_t::block_layer:
			{
				int const start = (m_mode == mode_t::piece_layer)
					? piece_layer_start()
					: block_layer_start();

				if (idx >= merkle_get_first_child(start)) return sha256_hash{};

				int layer_size = 1;
				while (idx < start)
				{
					idx = merkle_get_first_child(idx);
					layer_size *= 2;
				}

				idx -= start;
				if (idx >= m_tree.end_index())
					return merkle_pad(layer_size, 1);

				sha256_hash const pad_hash = (m_mode == mode_t::piece_layer)
					? merkle_pad(1 << m_blocks_per_piece_log, 1)
					: sha256_hash{};
				auto const layer= span<sha256_hash const>(m_tree)
					.subspan(idx, std::min(m_tree.end_index() - idx, layer_size));

				return merkle_root_scratch(layer, layer_size, pad_hash, scratch_space);
			}
		}
		TORRENT_ASSERT_FAIL();
		return sha256_hash{};
	}

	std::vector<sha256_hash> merkle_tree::build_vector() const
	{
		if (m_mode == mode_t::uninitialized_tree) return {};

		std::vector<sha256_hash> ret(size());

		switch (m_mode)
		{
			case mode_t::uninitialized_tree: break;
			case mode_t::empty_tree: break;
			case mode_t::full_tree:
				ret.assign(m_tree.begin(), m_tree.end());
				break;
			case mode_t::piece_layer:
			{
				int const num_leafs = merkle_num_leafs(m_num_blocks);
				int const piece_layer_size = merkle_num_leafs(num_pieces());
				sha256_hash const pad_hash = merkle_pad(num_leafs, piece_layer_size);
				int const start = merkle_first_leaf(piece_layer_size);
				TORRENT_ASSERT(m_tree.end_index() <= piece_layer_size);
				std::copy(m_tree.begin(), m_tree.end(), ret.begin() + start);
				std::fill(ret.begin() + start + m_tree.end_index(), ret.begin() + start + piece_layer_size, pad_hash);
				merkle_fill_tree(span<sha256_hash>(ret).subspan(0, merkle_num_nodes(piece_layer_size))
					, piece_layer_size);
				break;
			}
			case mode_t::block_layer:
			{
				int const num_leafs = merkle_num_leafs(m_num_blocks);
				sha256_hash const pad_hash{};
				int const start = merkle_first_leaf(num_leafs);
				std::copy(m_tree.begin(), m_tree.end(), ret.begin() + start);
				std::fill(ret.begin() + start + m_tree.end_index(), ret.begin() + start + num_leafs, sha256_hash{});
				merkle_fill_tree(ret, num_leafs);
				break;
			}
		}
		ret[0] = root();
		return ret;
	}

	std::pair<std::vector<sha256_hash>, aux::vector<bool>> merkle_tree::build_sparse_vector() const
	{
		if (m_mode == mode_t::uninitialized_tree) return {{}, {}};

		aux::vector<bool> mask(size(), false);
		std::vector<sha256_hash> ret;
		switch (m_mode)
		{
			case mode_t::uninitialized_tree: break;
			case mode_t::empty_tree: break;
			case mode_t::full_tree:
				for (int i = 0, end = m_tree.end_index(); i < end; ++i)
				{
					if (m_tree[i].is_all_zeros()) continue;
					ret.push_back(m_tree[i]);
					mask[i] = true;
				}
				break;
			case mode_t::piece_layer:
			{
				int const piece_layer_size = merkle_num_leafs(num_pieces());
				for (int i = merkle_first_leaf(piece_layer_size), end = i + m_tree.end_index(); i < end; ++i)
					mask[i] = true;
				ret = m_tree;
				break;
			}
			case mode_t::block_layer:
			{
				int const num_leafs = merkle_num_leafs(m_num_blocks);
				for (int i = merkle_first_leaf(num_leafs), end = i + m_tree.end_index(); i < end; ++i)
					mask[i] = true;
				ret = m_tree;
				break;
			}
		}
		return {std::move(ret), std::move(mask)};
	}

	void merkle_tree::allocate_full()
	{
		if (m_mode == mode_t::full_tree) return;
		m_tree = aux::vector<sha256_hash>(build_vector());
		m_mode = mode_t::full_tree;
	}

	void merkle_tree::optimize_storage()
	{
		if (m_mode != mode_t::full_tree) return;

		if (m_num_blocks == 1)
		{
			// if this tree *just* has a root, no need to use any storage for
			// nodes
			m_tree.clear();
			m_tree.shrink_to_fit();
			m_mode = mode_t::empty_tree;
			return;
		}

		int const start = block_layer_start();
		if (std::none_of(m_tree.begin() + start, m_tree.begin() + start + m_num_blocks
			, [](sha256_hash const& h) { return h.is_all_zeros(); }))
		{
			aux::vector<sha256_hash> new_tree(m_tree.begin() + start, m_tree.begin() + start + m_num_blocks);

			m_tree = std::move(new_tree);
			m_mode = mode_t::block_layer;
			return;
		}
	}

	void merkle_tree::optimize_storage_piece_layer()
	{
		if (m_mode != mode_t::full_tree) return;

		// if we have *any* blocks, we can't transition into piece layer mode,
		// since we would lose those hashes
		int const piece_layer_size = merkle_num_leafs(num_pieces());
		if (m_blocks_per_piece_log > 0
			&& merkle_validate_single_layer(span<sha256_hash const>(m_tree).subspan(0, merkle_num_nodes(piece_layer_size)))
			&& std::all_of(m_tree.begin() + block_layer_start(), m_tree.end(), [](sha256_hash const& h) { return h.is_all_zeros(); })
			)
		{
			int const start = piece_layer_start();
			aux::vector<sha256_hash> new_tree(m_tree.begin() + start, m_tree.begin() + start + num_pieces());

			m_tree = std::move(new_tree);
			m_mode = mode_t::piece_layer;
			return;
		}
	}

	std::vector<sha256_hash> merkle_tree::get_hashes(int const base
		, int const index, int const count, int const proof_layers) const
	{
		// given the full size of the tree, half of it, rounded up, are leaf nodes
		int const base_layer_idx = merkle_num_layers(num_leafs()) - base;
		int const base_start_idx = merkle_to_flat_index(base_layer_idx, index);

		int const layer_start_idx = base_start_idx;

		std::vector<sha256_hash> ret;
		ret.reserve(std::size_t(count));

		std::vector<sha256_hash> scratch_space;

		if (base == 0 && m_mode == mode_t::block_layer)
		{
			// this is an optimization
			int const blocks_end = std::min(index + count, m_num_blocks);
			for (int i = index; i < blocks_end; ++i)
				ret.push_back(m_tree[i]);
			// if fill the rest with padding
			ret.resize(std::size_t(count));
		}
		else
		{
			for (int i = layer_start_idx; i < layer_start_idx + count; ++i)
			{
				// the pad hashes are expected to be zero, they should not fail
				// the request
				if ((base != 0 || i < m_num_blocks + layer_start_idx - index)
					&& !has_node(i))
					return {};
				ret.push_back(get_impl(i, scratch_space));
			}
		}

		// the number of layers up the tree which can be computed from the base layer hashes
		// subtract one because the base layer doesn't count
		int const base_tree_layers = merkle_num_layers(merkle_num_leafs(count)) - 1;

		int proof_idx = layer_start_idx;
		for (int i = 0; i < proof_layers; ++i)
		{
			proof_idx = merkle_get_parent(proof_idx);

			// if this assert fire, the requester set proof_layers too high
			// and it wasn't correctly validated
			TORRENT_ASSERT(proof_idx > 0);

			if (i >= base_tree_layers)
			{
				int const sibling = merkle_get_sibling(proof_idx);
				if (!has_node(proof_idx) || !has_node(sibling))
					return {};

				ret.push_back(get_impl(sibling, scratch_space));
			}
		}

		return ret;
	}
}
