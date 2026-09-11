/*

Copyright (c) 2020, Alden Torres
Copyright (c) 2020-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/ffs.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/invariant_check.hpp"

namespace libtorrent {
namespace aux {

	merkle_tree::merkle_tree(int const num_blocks, int const blocks_per_piece, char const* r)
		: m_root(r)
		, m_num_blocks(num_blocks)
		, m_blocks_per_piece_log(numeric_cast<std::uint8_t>(
			log2p1(numeric_cast<std::uint32_t>(blocks_per_piece))))
		, m_mode(mode_t::empty_tree)
	{
		INVARIANT_CHECK;

		// blocks per piece must be an even power of 2
		TORRENT_ASSERT(((blocks_per_piece - 1) & blocks_per_piece) == 0);
		TORRENT_ASSERT(m_root != nullptr);
		TORRENT_ASSERT(this->blocks_per_piece() == blocks_per_piece);
	}

	sha256_hash merkle_tree::root() const { return sha256_hash(m_root); }

	void merkle_tree::load_verified_bits(bitfield const& verified)
	{
		TORRENT_ASSERT(m_block_verified.size() == m_num_blocks);

		// The verified bitfield may be invalid. If so, correct it to
		// maintain the invariant of this class
		int block_index = block_layer_start();
		for (int i = 0; i < std::min(int(verified.size()), m_num_blocks); ++i)
		{
			if (verified.get_bit(i) && has_node(block_index))
				m_block_verified.set_bit(i);
			++block_index;
		}
	}

	void merkle_tree::load_tree(span<sha256_hash const> t, bitfield const& verified)
	{
		INVARIANT_CHECK;
		if (t.empty()) return;
		if (root() != t[0]) return;
		if (size() != static_cast<std::size_t>(t.size())) return;

		if (t.size() == 1)
		{
			// don't fully allocate a tree of 1 node. It's just the root and we
			// have a special case representation for this
			optimize_storage();
			return;
		}

		allocate_full();

		merkle_validate_copy(t, m_tree, root(), m_block_verified);

		load_verified_bits(verified);

		optimize_storage();
		optimize_storage_piece_layer();
	}

	void merkle_tree::clear()
	{
		m_tree.clear();
		m_tree.shrink_to_fit();
		m_block_verified.clear();
		m_mode = mode_t::empty_tree;
	}

namespace {

	// TODO: in C++20, use std::identity
	struct identity
	{
		bool operator()(bool b) const { return b; }
	};
}

	void merkle_tree::load_sparse_tree(span<sha256_hash const> t
		, bitfield const& mask
		, bitfield const& verified)
	{
		INVARIANT_CHECK;

		// The size of the mask should not exceed the size of the tree, but a mask larger than
		// the tree can be encountered in the "resume data" for some reason, perhaps there is
		// a bug in the "resume data" generation algorithm.
		// So we just process mask items count up to tree size.
		int const mask_size = std::min(mask.size(), int(size()));

		int const first_block = block_layer_start();
		int const end_block = first_block + m_num_blocks;

		TORRENT_ASSERT(first_block < int(size()));
		TORRENT_ASSERT(end_block <= int(size()));

		// if the mask covers all blocks, go straight to block_layer
		// mode, and validate
		if ((first_block < mask_size) && (end_block <= mask_size)
			&& std::all_of(mask.begin() + first_block, mask.begin() + end_block, identity()))
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

			TORRENT_ASSERT(first_piece < int(size()));
			TORRENT_ASSERT(end_piece <= int(size()));

			// if the mask covers all pieces, and nothing below that layer, go
			// straight to piece_layer mode and validate
			if ((first_piece < mask_size) && (end_piece <= mask_size)
				&& std::all_of(mask.begin() + first_piece, mask.begin() + end_piece, identity())
				&& std::all_of(mask.begin() + end_piece, mask.begin() + mask_size, std::logical_not<>()))
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
		if (t.empty() || std::none_of(mask.begin(), mask.begin() + mask_size, identity()))
			return clear();

		allocate_full();
		int cursor = 0;
		for (int i = 0, end = mask_size; i < end; ++i)
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

		load_verified_bits(verified);

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
					auto const leafs = span<sha256_hash const>(m_tree).subspan(
						b, std::min(blocks_in_piece, int(m_tree.size()) - b));
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
		INVARIANT_CHECK;
		if (m_mode == mode_t::block_layer) return true;

		int const npieces = num_pieces();
		if (piece_layer.size() != npieces * sha256_hash::size()) return false;

		if (m_num_blocks == 1)
		{
			// special case for trees that only have a root hash
			if (sha256_hash(piece_layer.data()) != root())
				return false;
			m_mode = mode_t::empty_tree;
			m_tree.clear();
			m_block_verified.clear();
			return true;
		}

		sha256_hash const pad_hash = merkle_pad(1 << m_blocks_per_piece_log, 1);

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
		// piece_layer and block_layer modes both require m_block_verified
		// to be empty (see check_invariant). The verified bitfield only
		// has meaning when paired with the full_tree storage; the
		// optimized modes derive verification from the mode itself.
		m_block_verified.clear();

		return true;
	}

	// dest_start_idx points to the first *leaf* to be added.
	// For example, T is the sub-tree to insert into the larger tree
	// the uncle hashes are specified as 0, 1, providing proof that the subtree
	// is valid, since the root node can be computed and validated.
	// The root of the tree, R, is always known.
	//                         R
	//            _                          _
	//     _              1           _              _
	//  _     _        _     _     T     0        _     _
	//_   _ _   _    _   _ _   _ T   T _   _    _   _ _   _
	//                           ^
	//                           |
	//                           dest_start_idx
	std::optional<add_hashes_result_t> merkle_tree::add_hashes(
		int const dest_start_idx
		, piece_index_t::diff_type const file_piece_offset
		, span<sha256_hash const> hashes
		, span<sha256_hash const> uncle_hashes)
	{
		INVARIANT_CHECK;

		// as we set the hashes of interior nodes, we may be able to validate
		// block hashes that we had since earlier. Any blocks that can be
		// validated, and failed, are added to this list
		add_hashes_result_t ret;

		// we already have all hashes
		if (m_mode == mode_t::block_layer)
		{
			// since we're already on the block layer mode, we have the whole
			// tree, and we've already reported any pieces as passing that may
			// have existed in the tree when we completed it. At this point no
			// more pieces should be reported as passed
			return ret;
		}

		allocate_full();

		// TODO: this can be optimized by using m_tree as storage to fill this
		// tree into, and then clear it if the hashes fail
		int const leaf_count = merkle_num_leafs(int(hashes.size()));

		// hashes.size() (and so leaf_count, once rounded up to a power of 2)
		// is not bounds-checked against the destination layer by the caller,
		// only the un-rounded count is. Reject any request whose padded span
		// would run past the end of the layer it's inserted into.
		int const dest_layer_size = 1 << merkle_get_layer(dest_start_idx);
		int const dest_layer_offset = merkle_get_layer_offset(dest_start_idx);
		if (dest_layer_offset + leaf_count > dest_layer_size)
			return {};

		aux::vector<sha256_hash> tree(merkle_num_nodes(leaf_count));
		std::copy(hashes.begin(), hashes.end(), tree.end() - leaf_count);

		// the end of a file is a special case, we may need to pad the leaf layer
		if (leaf_count > hashes.size())
		{
			int const leaf_layer_size = num_leafs();
			// assuming uncle_hashes lead all the way to the root, they tell us
			// how many layers down we are
			// uncle_hashes.size() is caller-controlled; bound it before using it
			// as a shift exponent below, it's otherwise unrelated to leaf_count
			int const max_proof_layers =
				merkle_num_layers(leaf_layer_size) - merkle_num_layers(leaf_count);
			if (uncle_hashes.size() > max_proof_layers)
				return {};
			int const insert_layer_size = leaf_count << uncle_hashes.size();
			if (leaf_layer_size != insert_layer_size)
			{
				sha256_hash const pad_hash = merkle_pad(leaf_layer_size, insert_layer_size);
				for (int i = int(hashes.size()); i < leaf_count; ++i)
					tree[tree.end_index() - leaf_count + i] = pad_hash;
			}
		}

		merkle_fill_tree(tree, leaf_count);

		int const base_num_layers = merkle_num_layers(leaf_count);

		// this is the index of the node where we'll insert the root of the
		// subtree (tree). It's also the hash the uncle_hashes are here to prove
		// is valid.
		int const insert_root_idx = dest_start_idx >> base_num_layers;

		// first fill in the subtree of known hashes from the base layer
		auto const num_leafs = merkle_num_leafs(m_num_blocks);
		auto const first_leaf = merkle_first_leaf(num_leafs);

		// merkle_validate_and_insert_proofs() below writes insert_root_idx's
		// slot, and its sibling's if it's a leaf, the first time either is
		// seen, so has_node() can no longer tell "already known" from
		// "just proven" afterward. Snapshot both first: only an
		// already-known hash is backed by downloaded data (set_block()),
		// and only that may be reported as passed below.
		bool const insert_root_already_known = has_node(insert_root_idx);
		// insert_root_idx > 0 since the root node has no sibling.
		bool const sibling_already_known = !uncle_hashes.empty() && insert_root_idx >= first_leaf
			&& insert_root_idx > 0 && insert_root_idx - first_leaf < m_num_blocks
			&& has_node(merkle_get_sibling(insert_root_idx));

		// start with validating the proofs, and inserting them as we go.
		if (!merkle_validate_and_insert_proofs(m_tree, insert_root_idx, tree[0], uncle_hashes))
			return {};

		// if insert_root_idx is a leaf, the walk above may have just
		// populated its sibling leaf too (the first uncle hash), which the
		// main loop below won't see since it only walks "hashes". A
		// successful return means anything touched is proven correct. With
		// no uncle hashes there was no walk, so the sibling wasn't touched
		// and nothing was learned about it.
		if (!uncle_hashes.empty() && insert_root_idx > 0 && insert_root_idx >= first_leaf
			&& insert_root_idx - first_leaf < m_num_blocks)
		{
			// the root node has no sibling.
			TORRENT_ASSERT(insert_root_idx > 0);
			int const sibling_idx = merkle_get_sibling(insert_root_idx);
			int const sibling_block = sibling_idx - first_leaf;
			if (sibling_block >= 0 && sibling_block < m_num_blocks)
			{
				// merkle_validate_and_insert_proofs() only returns true once
				// it has hashed this sibling together with insert_root_idx
				// into a proven ancestor, so m_tree[sibling_idx] is
				// necessarily non-zero here.
				TORRENT_ASSERT(!m_tree[sibling_idx].is_all_zeros());
				bool const already_verified = m_block_verified.get_bit(sibling_block);
				m_block_verified.set_bit(sibling_block);

				// when a piece is a single block, a verified block is a
				// verified piece. Otherwise, the piece can only pass once all
				// of its blocks are known, which is handled below. Only
				// report it the first time it becomes verified, since a
				// later call may re-learn the same sibling hash via a
				// different proof.
				if (m_blocks_per_piece_log == 0 && !already_verified && sibling_already_known)
				{
					auto const piece = piece_index_t{sibling_block} + file_piece_offset;
					if (ret.passed.empty() || ret.passed.back() != piece)
						ret.passed.push_back(piece);
				}
			}
		}

		// this is the start of the leaf layer of "tree". We'll use this
		// variable to step upwards towards the root
		int source_cursor = int(tree.size()) - leaf_count;
		// the running index in the loop
		int dest_cursor = dest_start_idx;

		// the number of tree levels in a piece hash. 0 means the block layer is
		// the same as the piece layer
		int const base = piece_levels();

		// hash_picker only ever requests whole, piece-aligned ranges at the
		// block layer (either a single piece, or the entire block layer in
		// one call), so an insertion here must never start or end in the
		// middle of a piece. This is what makes the running
		// current_piece_matched count below sound: a piece's blocks always
		// arrive together in the same call.
#if TORRENT_USE_ASSERTS
		if (dest_start_idx >= first_leaf)
		{
			int const blocks_per_piece = 1 << base;
			int const pos = dest_start_idx - first_leaf;
			TORRENT_ASSERT(pos % blocks_per_piece == 0);
			TORRENT_ASSERT(leaf_count % blocks_per_piece == 0 || pos + leaf_count == m_num_blocks);
		}
#endif

		// count of the current piece's matched block hashes, needed because a
		// single matching leaf does not prove a multi-block piece is valid.
		// The leaf layer below is scanned once in increasing order, so pieces
		// are never revisited and a running count reset per piece suffices.
		auto current_piece = piece_index_t(-1);
		int current_piece_matched = 0;

		// TODO: a piece outside of this range may also fail, if one of the uncle
		// hashes is at the layer right above the block hashes
		for (int layer_size = leaf_count; layer_size != 0; layer_size /= 2)
		{
			for (int i = 0; i < layer_size; ++i)
			{
				int const dst_idx = dest_cursor + i;
				int const src_idx = source_cursor + i;
				// dst_idx == insert_root_idx was already written above by
				// merkle_validate_and_insert_proofs()
				if (dst_idx == insert_root_idx ? insert_root_already_known : has_node(dst_idx))
				{
					if (m_tree[dst_idx] != tree[src_idx])
					{
						// this must be a block hash because inner nodes are not filled in until
						// they can be verified. This assert ensures we're at the
						// leaf layer of the file tree
						TORRENT_ASSERT(dst_idx >= first_leaf);
						int const pos = dst_idx - first_leaf;
						int const block = pos & ((1 << m_blocks_per_piece_log) - 1);
						auto const piece =
							piece_index_t{pos >> m_blocks_per_piece_log} + file_piece_offset;
						TORRENT_ASSERT(pos < m_num_blocks);

						if (!ret.failed.empty() && ret.failed.back().first == piece)
							ret.failed.back().second.push_back(block);
						else
							ret.failed.emplace_back(piece, std::vector<int>{block});

						// now that this hash has been reported as failing, we
						// can clear it. This will prevent it from being
						// reported as failing again.
						m_tree[dst_idx].clear();
					}
					else if (dst_idx >= first_leaf)
					{
						int const pos = dst_idx - first_leaf;
						int const block = pos & ((1 << m_blocks_per_piece_log) - 1);
						auto const piece =
							piece_index_t{pos >> m_blocks_per_piece_log} + file_piece_offset;

						// padding leaves are always zero (see check_invariant()),
						// so they never reach this branch
						TORRENT_ASSERT(pos < m_num_blocks);

						if (piece != current_piece)
						{
							current_piece = piece;
							current_piece_matched = 0;
						}

						int const piece_block_start = pos - block;
						int const piece_blocks =
							std::min(blocks_per_piece(), m_num_blocks - piece_block_start);

						if (++current_piece_matched == piece_blocks)
						{
							// piece order is strictly increasing, so this can
							// only trigger once per piece
							TORRENT_ASSERT(ret.passed.empty() || ret.passed.back() != piece);
							ret.passed.push_back(piece);
						}
					}
				}

				if (dst_idx >= first_leaf && dst_idx - first_leaf < m_num_blocks)
					m_block_verified.set_bit(dst_idx - first_leaf);

				m_tree[dst_idx] = tree[src_idx];
			}
			if (layer_size == 1) break;
			dest_cursor = merkle_get_parent(dest_cursor);
			source_cursor = merkle_get_parent(source_cursor);
		}

		// if the piece layer and the block layer is the same, we have already
		// identified all the failing hashes in the loop above. This is covering
		// the cases where we just learned about piece level hashes and we can
		// validate the block hashes for those pieces.
		int const first_piece_idx = piece_layer_start();
		if (base != 0
			&& dest_start_idx >= first_piece_idx
			&& dest_start_idx < first_piece_idx + num_pieces())
		{
			int const blocks_in_piece = 1 << base;

			// it may now be possible to verify the hashes of previously received blocks
			// try to verify as many child nodes of the received hashes as possible
			for (int i = 0; i < int(hashes.size()); ++i)
			{
				int const piece = dest_start_idx + i;
				if (piece - first_piece_idx >= num_pieces())
					break;
				// the first block in this piece
				int const block_idx = merkle_get_first_child(piece, base);

				int const block_end_idx = std::min(block_idx + blocks_in_piece, first_leaf + m_num_blocks);
				if (std::any_of(m_tree.begin() + block_idx
					, m_tree.begin() + block_end_idx
					, [](sha256_hash const& h) { return h.is_all_zeros(); }))
					continue;

				// TODO: instead of overwriting the root and comparing it
				// against hashes[], write a functions that *validates* a tree
				// by just filling it up to the level below the root and then
				// validates it.
				merkle_fill_tree(m_tree, blocks_in_piece, block_idx);
				if (m_tree[piece] != hashes[i])
				{
					merkle_clear_tree(m_tree, blocks_in_piece, block_idx);
					// write back the correct hash
					m_tree[piece] = hashes[i];
					TORRENT_ASSERT(blocks_in_piece == blocks_per_piece());

					// an empty blocks vector indicates that we don't have the
					// block hashes, and we can't know which block failed
					// this will cause the block hashes to be requested
					ret.failed.emplace_back(piece_index_t{piece - first_piece_idx} + file_piece_offset
						, std::vector<int>());
				}
				else
				{
					ret.passed.push_back(piece_index_t{piece - first_piece_idx} + file_piece_offset);
					// record that these block hashes are correct!
					int const leafs_start = block_idx - block_layer_start();
					int const leafs_end = std::min(m_num_blocks, leafs_start + blocks_in_piece);
					// TODO: this could be done more efficiently if bitfield had a function
					// to set a range of bits
					for (int k = leafs_start; k < leafs_end; ++k)
						m_block_verified.set_bit(k);
				}
				TORRENT_ASSERT((piece - first_piece_idx) >= 0);
			}
		}

		optimize_storage();

		return ret;
	}

	std::tuple<merkle_tree::set_block_result, int, int> merkle_tree::set_block(int const block_index
		, sha256_hash const& h)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(block_index < m_num_blocks);

		auto const num_leafs = merkle_num_leafs(m_num_blocks);
		auto const first_leaf = merkle_first_leaf(num_leafs);
		auto const block_tree_index = first_leaf + block_index;

		if (blocks_verified(block_index, 1))
		{
			// if this blocks's hash is already known, check the passed-in hash against it
			if (compare_node(block_tree_index, h))
				return std::make_tuple(set_block_result::ok, block_index, 1);
			else
				return std::make_tuple(set_block_result::block_hash_failed, block_index, 1);
		}

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
		merkle_fill_tree(m_tree, leafs_size, first_leaf + leafs_start);

		if (root != m_tree[root_index])
		{
			// hash failure, clear all the internal nodes
			// the whole piece failed the hash check. Clear all block hashes
			// in this piece and report a hash failure
			//
			// we can't tell which block in the subtree is wrong, only that
			// their combined hash no longer matches, so none of them can
			// still be vouched for individually
			merkle_clear_tree(m_tree, leafs_size, first_leaf + leafs_start);
			m_tree[root_index] = root;
			return std::make_tuple(set_block_result::hash_failed, leafs_start, leafs_size);
		}

		// TODO: this could be done more efficiently if bitfield had a function
		// to set a range of bits
		int const leafs_end = std::min(m_num_blocks, leafs_start + leafs_size);
		for (int i = leafs_start; i < leafs_end; ++i)
			m_block_verified.set_bit(i);

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
		TORRENT_ASSERT(idx >= 0);
		TORRENT_ASSERT(idx < int(size()));
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
			case mode_t::empty_tree:
				return idx == 0 ? root() == h : h.is_all_zeros();
			case mode_t::full_tree:
				return m_tree[idx] == h;
			case mode_t::piece_layer:
			{
				int const first = piece_layer_start();
				int const piece_count = num_pieces();
				int const pieces_end = first + piece_count;
				int const piece_layer_size = merkle_num_leafs(piece_count);
				int const end = first + piece_layer_size;
				if (idx >= end)
					return h.is_all_zeros();
				if (idx >= pieces_end)
					return h == merkle_pad(1 << m_blocks_per_piece_log, 1);
				if (idx >= first)
					return m_tree[idx - first] == h;
				return (*this)[idx] == h;
			}
			case mode_t::block_layer:
			{
				int const first = block_layer_start();
				int const end = first + m_num_blocks;
				if (idx >= end)
					return h.is_all_zeros();
				if (idx >= first)
					return m_tree[idx - first] == h;
				return (*this)[idx] == h;
			}
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
			case mode_t::empty_tree:
				return idx == 0 ? root() : sha256_hash{};
			case mode_t::full_tree:
				return m_tree[idx];
			case mode_t::piece_layer:
			case mode_t::block_layer:
			{
				int const start = (m_mode == mode_t::piece_layer)
					? piece_layer_start()
					: block_layer_start();

				if (m_mode == mode_t::piece_layer && idx >= merkle_get_first_child(start))
					return {};

				int layer_size = 1;
				while (idx < start)
				{
					idx = merkle_get_first_child(idx);
					layer_size *= 2;
				}

				idx -= start;
				if (idx >= m_tree.end_index())
				{
					return merkle_pad(
						(m_mode == mode_t::piece_layer)
						? layer_size << m_blocks_per_piece_log
						: layer_size, 1);
				}

				sha256_hash const pad_hash = (m_mode == mode_t::piece_layer)
					? merkle_pad(1 << m_blocks_per_piece_log, 1)
					: sha256_hash{};
				auto const layer = span<sha256_hash const>(m_tree)
					.subspan(idx, std::min(m_tree.end_index() - idx, layer_size));

				return merkle_root_scratch(layer, layer_size, pad_hash, scratch_space);
			}
		}
		TORRENT_ASSERT_FAIL();
		return sha256_hash{};
	}

	std::vector<sha256_hash> merkle_tree::build_vector() const
	{
		INVARIANT_CHECK;
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
				int const piece_layer_size = merkle_num_leafs(num_pieces());
				sha256_hash const pad_hash = merkle_pad(1 << m_blocks_per_piece_log, 1);
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

	std::pair<std::vector<sha256_hash>, bitfield> merkle_tree::build_sparse_vector() const
	{
		if (m_mode == mode_t::uninitialized_tree) return {{}, {}};

		bitfield mask{int(size())};
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
					mask.set_bit(i);
				}
				break;
			case mode_t::piece_layer:
			{
				int const piece_layer_size = merkle_num_leafs(num_pieces());
				for (int i = merkle_first_leaf(piece_layer_size), end = i + m_tree.end_index(); i < end; ++i)
					mask.set_bit(i);
				ret = m_tree;
				break;
			}
			case mode_t::block_layer:
			{
				int const num_leafs = merkle_num_leafs(m_num_blocks);
				for (int i = merkle_first_leaf(num_leafs), end = i + m_tree.end_index(); i < end; ++i)
					mask.set_bit(i);
				ret = m_tree;
				break;
			}
		}
		return {std::move(ret), std::move(mask)};
	}

	bitfield merkle_tree::verified_leafs() const
	{
		// note that for an empty tree (where the root is the full tree) and a
		// tree where we have the piece layer, we also know all leaves in case
		// the piece size is a single block.
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
			case mode_t::empty_tree:
				return {m_num_blocks, m_num_blocks == 1};
			case mode_t::piece_layer:
				return {m_num_blocks, piece_levels() == 0};
			case mode_t::block_layer:
				return {m_num_blocks, true};
			case mode_t::full_tree:
				return m_block_verified;
		}
		TORRENT_ASSERT_FAIL();
		return {};
	}

	bool merkle_tree::is_complete() const
	{
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				return false;
			case mode_t::empty_tree:
				return m_num_blocks == 1;
			case mode_t::piece_layer:
				return piece_levels() == 0;
			case mode_t::block_layer:
				return true;
			case mode_t::full_tree:
				return !m_block_verified.empty() && m_block_verified.all_set();
		}
		TORRENT_ASSERT_FAIL();
		return false;
	}

	bool merkle_tree::blocks_verified(int block_idx, int num_blocks) const
	{
		TORRENT_ASSERT(num_blocks > 0);
		TORRENT_ASSERT(block_idx < m_num_blocks);
		TORRENT_ASSERT(block_idx + num_blocks <= m_num_blocks);
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				return false;
			case mode_t::empty_tree:
				return m_num_blocks == 1;
			case mode_t::piece_layer:
				return piece_levels() == 0;
			case mode_t::block_layer:
				return true;
			case mode_t::full_tree:
				for (int i = block_idx; i < block_idx + num_blocks; ++i)
					if (!m_block_verified.get_bit(i)) return false;
				return true;
		}
		TORRENT_ASSERT_FAIL();
		return false;
	}

	void merkle_tree::allocate_full()
	{
		if (m_mode == mode_t::full_tree) return;

		INVARIANT_CHECK;

		// if we already have the complete tree, we shouldn't be allocating it
		// again.
		TORRENT_ASSERT(m_mode != mode_t::block_layer);
		m_tree = aux::vector<sha256_hash>(build_vector());
		m_mode = mode_t::full_tree;
		m_block_verified.resize(m_num_blocks, false);

		// a single block equals the root hash, so it's implicitly verified,
		// an invariant check_invariant() relies on.
		if (m_num_blocks == 1)
			m_block_verified.set_bit(0);
	}

	void merkle_tree::optimize_storage()
	{
		INVARIANT_CHECK;
		if (m_mode != mode_t::full_tree) return;

		if (m_num_blocks == 1)
		{
			// if this tree *just* has a root, no need to use any storage for
			// nodes
			m_tree.clear();
			m_tree.shrink_to_fit();
			m_mode = mode_t::empty_tree;
			m_block_verified.clear();
			return;
		}

		int const start = block_layer_start();
		if (m_block_verified.all_set())
		{
			aux::vector<sha256_hash> new_tree(m_tree.begin() + start, m_tree.begin() + start + m_num_blocks);

			m_tree = std::move(new_tree);
			m_mode = mode_t::block_layer;
			m_block_verified.clear();
			return;
		}
	}

	void merkle_tree::optimize_storage_piece_layer()
	{
		INVARIANT_CHECK;
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
			m_block_verified.clear();
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

		// proof_idx climbs one layer per iteration, starting at
		// layer_start_idx (i == 0). Whether *its* sibling at that layer
		// needs to be sent depends on how tall a subtree the count leaves,
		// already gathered above, form on their own: every node of that
		// subtree, at every one of its layers, is implied by those leaves.
		// Example with count == 4 (implied_layers == 2: the subtree is 2
		// layers tall):
		//
		//    i    proof_idx's sibling
		//   ---   ----------------------------------------------------
		//    2    not implied, must be pushed (first hash sent to peer)
		//    1    implied: still inside the 4-leaf subtree, both
		//         children already sit in `ret`
		//    0    implied: layer_start_idx's sibling is itself one of
		//         the other 3 requested leaves, already in `ret`
		//
		// pushed whenever i >= implied_layers. count == 1 has no subtree
		// at all (implied_layers == 0), so even the leaf's own sibling at
		// i == 0 isn't implied, and gets pushed too
		int const implied_layers = merkle_num_layers(merkle_num_leafs(count));

		int proof_idx = layer_start_idx;
		for (int i = 0; i < implied_layers; ++i, proof_idx = merkle_get_parent(proof_idx))
		{
			// if this assert fires, the requester set proof_layers too high
			// and it wasn't correctly validated
			TORRENT_ASSERT(proof_idx > 0);
		}

		for (int i = implied_layers; i <= proof_layers;
			 ++i, proof_idx = merkle_get_parent(proof_idx))
		{
			// if this assert fires, the requester set proof_layers too high
			// and it wasn't correctly validated
			TORRENT_ASSERT(proof_idx > 0);

			int const sibling = merkle_get_sibling(proof_idx);

			// a sibling past the last real block is a padding leaf; its
			// value is always the well-known all-zero leaf pad, never a
			// stored node. padding only exists at the leaf layer
			bool const sibling_is_real =
				i > 0 || base != 0 || sibling < m_num_blocks + layer_start_idx - index;

			if (!has_node(proof_idx) || (sibling_is_real && !has_node(sibling)))
				return {};

			ret.push_back(sibling_is_real ? get_impl(sibling, scratch_space) : sha256_hash{});
		}

		return ret;
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void merkle_tree::check_invariant() const
	{
		if (m_num_blocks == 1 && m_mode != mode_t::uninitialized_tree)
		{
			// a tree with only a single block is implicitly verified since we
			// have the root hash (unless the tree is uninitialized)
			TORRENT_ASSERT(blocks_verified(0, 1));
		}
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				TORRENT_ASSERT(m_tree.empty());
				TORRENT_ASSERT(m_block_verified.empty());
				break;
			case mode_t::empty_tree:
				TORRENT_ASSERT(m_tree.empty());
				TORRENT_ASSERT(m_block_verified.empty());
				break;
			case mode_t::full_tree:
			{
				TORRENT_ASSERT(m_tree[0] == root());
				TORRENT_ASSERT(m_block_verified.size() == m_num_blocks);

				auto const num_leafs = merkle_num_leafs(m_num_blocks);

				if (m_tree.size() == 1) break;

				// In all layers, except the block layer, all non-zero hashes
				// must have a non-zero sibling and they must validate with
				// their parent.
				for (int i = 1; i < int(m_tree.size()) - num_leafs; i += 2)
				{
					if (m_tree[i].is_all_zeros())
					{
						TORRENT_ASSERT(m_tree[i + 1].is_all_zeros());
						continue;
					}
					TORRENT_ASSERT(merkle_validate_node(m_tree[i], m_tree[i+1], m_tree[merkle_get_parent(i)]));
				}

				// the block layer may contain invalid hashes, but if the
				// corresponding bit in m_block_verified is 1, they must be
				// correct, and match the block hashes.
				// validate all blocks (that can be validated)
				// since these are checked in pairs, we skip 2, to always
				// consider the left side of the pair
				auto const first_block = merkle_first_leaf(num_leafs);
				for (int i = first_block, b = 0; i < first_block + m_num_blocks; i += 2, b += 2)
				{
					if (i + 1 == first_block + m_num_blocks)
					{
						// the edge case where there's an odd number of blocks
						// and this is the last one
						if (!m_block_verified.get_bit(b)) continue;
						TORRENT_ASSERT(has_node(i));
						int const parent = merkle_get_parent(i);
						TORRENT_ASSERT(merkle_validate_node(m_tree[i], sha256_hash(), m_tree[parent]));
					}
					else
					{
						TORRENT_ASSERT(m_block_verified.get_bit(b) == m_block_verified.get_bit(b + 1));
						if (!m_block_verified.get_bit(b)) continue;
						TORRENT_ASSERT(has_node(i));

						if (i + 1 < block_layer_start() + m_num_blocks)
							TORRENT_ASSERT(has_node(i + 1));
						int const parent = merkle_get_parent(i);
						TORRENT_ASSERT(merkle_validate_node(m_tree[i], m_tree[i + 1], m_tree[parent]));
					}
				}
				// ensure padding is all zeros
				for (int i = first_block + m_num_blocks; i < int(m_tree.size()); ++i)
					TORRENT_ASSERT(m_tree[i].is_all_zeros());
				break;
			}
			case mode_t::piece_layer:
			{
				TORRENT_ASSERT(merkle_root(m_tree, merkle_pad(1 << m_blocks_per_piece_log, 1)) == root());
				TORRENT_ASSERT(m_block_verified.empty());
				break;
			}
			case mode_t::block_layer:
			{
				TORRENT_ASSERT(merkle_root(m_tree) == root());
				TORRENT_ASSERT(m_block_verified.empty());
				break;
			}
		}
	}
#endif
}
}
