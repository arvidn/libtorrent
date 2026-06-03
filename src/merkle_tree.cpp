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

		// fill the per-layer offsets table for the compact layout. layer L
		// stores only its live (non-padding) nodes in m_tree, packed:
		// m_layer_offsets[L] is the start of layer L; the last entry holds
		// the total compact size. Depends only on m_num_blocks.
		if (m_num_blocks > 0)
		{
			int const n_layers = merkle_num_layers(merkle_num_leafs(m_num_blocks));
			m_layer_offsets.resize(n_layers + 2);
			m_layer_offsets[0] = 0;
			for (int L = 0; L <= n_layers; ++L)
			{
				int const shift = n_layers - L;
				int const live = (m_num_blocks + (1 << shift) - 1) >> shift;
				m_layer_offsets[L + 1] = m_layer_offsets[L] + live;
			}
		}
	}

	sha256_hash merkle_tree::root() const { return sha256_hash(m_root); }

	void merkle_tree::load_verified_bits(bitfield const& verified)
	{
		TORRENT_ASSERT(m_block_verified.size() == m_num_blocks);

		// The verified bitfield may be invalid. If so, correct it to
		// maintain the invariant of this class
		int const leaf_L = num_layers();
		for (int i = 0; i < std::min(int(verified.size()), m_num_blocks); ++i)
		{
			if (verified.get_bit(i) && has_node(leaf_L, i)) m_block_verified.set_bit(i);
		}
	}

	void merkle_tree::load_tree(span<sha256_hash const> t, bitfield const& verified)
	{
		INVARIANT_CHECK;
		if (t.empty()) return;
		if (root() != t[0]) return;
		if (size() != static_cast<std::size_t>(t.size())) return;

		// Input validates; install it regardless of our current mode by
		// resetting to empty_tree first. This lets callers invoke
		// load_tree() repeatedly without having to know whether a prior
		// load or set_block has already collapsed us into block_layer
		// (which would otherwise assert inside allocate_compact).
		clear();

		if (t.size() == 1)
		{
			// don't fully allocate a tree of 1 node. It's just the root and we
			// have a special case representation for this
			return;
		}

		allocate_compact();

		merkle_validate_copy(t, *this, root(), m_block_verified);

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

		// Reset to empty_tree so the load is well-defined regardless of our
		// current mode. The fast paths below would overwrite m_tree anyway,
		// but the general-case path runs allocate_compact, which asserts
		// against m_mode == block_layer; clearing here lets a caller
		// re-load over any prior state.
		clear();

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

		allocate_compact();
		int cursor = 0;
		for (int i = 0, end = mask_size; i < end; ++i)
		{
			if (!mask[i]) continue;
			if (cursor >= t.size()) break;
			int const L = merkle_get_layer(int(i));
			set(L, int(i) - merkle_layer_start(L), t[cursor++]);
		}
		merkle_fill_partial_tree(*this);

		// this suggests that none of the hashes in the tree can be
		// validated against the root. We effectively have an empty tree.
		if (get(0, 0) != root()) return clear();

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
			case mode_t::full_tree: {
				int const piece_L = num_layers() - piece_levels();
				int const piece_start = m_layer_offsets[piece_L];
				ret.assign(
					m_tree.begin() + piece_start, m_tree.begin() + piece_start + num_pieces());
				break;
			}
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

		allocate_compact();

		// TODO: this can be optimized by using m_tree as storage to fill this
		// tree into, and then clear it if the hashes fail
		int const leaf_count = merkle_num_leafs(int(hashes.size()));
		aux::vector<sha256_hash> tree(merkle_num_nodes(leaf_count));
		std::copy(hashes.begin(), hashes.end(), tree.end() - leaf_count);

		// the end of a file is a special case, we may need to pad the leaf layer
		if (leaf_count > hashes.size())
		{
			int const leaf_layer_size = num_leafs();
			// assuming uncle_hashes lead all the way to the root, they tell us
			// how many layers down we are
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

		// (level, offset) of the node where we'll insert the root of the
		// subtree (tree). It's also the hash the uncle_hashes are here to prove
		// is valid.
		int const insert_root_flat = dest_start_idx >> base_num_layers;
		int const insert_root_L = merkle_get_layer(insert_root_flat);
		int const insert_root_O = insert_root_flat - merkle_layer_start(insert_root_L);

		// start with validating the proofs, and inserting them as we go.
		if (!merkle_validate_and_insert_proofs(
				*this, insert_root_L, insert_root_O, tree[0], uncle_hashes))
			return {};

		// first fill in the subtree of known hashes from the base layer

		// this is the start of the leaf layer of "tree". We'll use this
		// variable to step upwards towards the root
		int source_cursor = int(tree.size()) - leaf_count;

		// (level, offset) of the insertion cursor in m_tree. The walk
		// steps up one layer per outer iteration.
		int dest_L = merkle_get_layer(dest_start_idx);
		int dest_O_base = dest_start_idx - merkle_layer_start(dest_L);

		// the number of tree levels in a piece hash. 0 means the block layer is
		// the same as the piece layer
		int const base = piece_levels();

		// TODO: a piece outside of this range may also fail, if one of the uncle
		// hashes is at the layer right above the block hashes
		for (int layer_size = leaf_count; layer_size != 0; layer_size /= 2)
		{
			for (int i = 0; i < layer_size; ++i)
			{
				int const dst_O = dest_O_base + i;
				int const src_idx = source_cursor + i;
				if (has_node(dest_L, dst_O))
				{
					if (get(dest_L, dst_O) != tree[src_idx])
					{
						// this must be a block hash because inner nodes are not filled in until
						// they can be verified. This assert ensures we're at the
						// leaf layer of the file tree
						TORRENT_ASSERT(dest_L == num_layers());

						int const pos = dst_O;
						auto const piece = piece_index_t{pos >> m_blocks_per_piece_log} + file_piece_offset;
						int const block = pos & ((1 << m_blocks_per_piece_log) - 1);

						TORRENT_ASSERT(pos < m_num_blocks);
						if (!ret.failed.empty() && ret.failed.back().first == piece)
							ret.failed.back().second.push_back(block);
						else
							ret.failed.emplace_back(piece, std::vector<int>{block});

						// now that this hash has been reported as failing, we
						// can clear it. This will prevent it from being
						// reported as failing again.
						set(dest_L, dst_O, sha256_hash{});
					}
					else if (dest_L == num_layers())
					{
						// this covers the case where pieces are a single block.
						// The common case is covered below
						auto const piece =
							piece_index_t{dst_O >> m_blocks_per_piece_log} + file_piece_offset;

						if (ret.passed.empty() || ret.passed.back() != piece)
							ret.passed.push_back(piece);
					}
				}

				if (dest_L == num_layers() && dst_O < m_num_blocks) m_block_verified.set_bit(dst_O);

				set(dest_L, dst_O, tree[src_idx]);
			}
			if (layer_size == 1) break;
			source_cursor = merkle_get_parent(source_cursor);
			dest_O_base /= 2;
			--dest_L;
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
			int const leaf_L = num_layers();
			int const piece_L = leaf_L - base;

			// it may now be possible to verify the hashes of previously received blocks
			// try to verify as many child nodes of the received hashes as possible
			for (int i = 0; i < int(hashes.size()); ++i)
			{
				int const piece = dest_start_idx + i;
				int const piece_O = piece - first_piece_idx;
				if (piece_O >= num_pieces()) break;
				// the first block in this piece
				int const block_idx = merkle_get_first_child(piece, base);
				int const block_O = block_idx - block_layer_start();

				int const block_O_end = std::min(block_O + blocks_in_piece, m_num_blocks);
				bool any_zero = false;
				for (int k = block_O; k < block_O_end; ++k)
				{
					if (get(leaf_L, k).is_all_zeros())
					{
						any_zero = true;
						break;
					}
				}
				if (any_zero) continue;

				// TODO: instead of overwriting the root and comparing it
				// against hashes[], write a functions that *validates* a tree
				// by just filling it up to the level below the root and then
				// validates it.
				merkle_fill_tree(*this, leaf_L, block_O, blocks_in_piece);
				if (get(piece_L, piece_O) != hashes[i])
				{
					merkle_clear_tree(*this, leaf_L, block_O, blocks_in_piece);
					// write back the correct hash
					set(piece_L, piece_O, hashes[i]);
					TORRENT_ASSERT(blocks_in_piece == blocks_per_piece());

					// an empty blocks vector indicates that we don't have the
					// block hashes, and we can't know which block failed
					// this will cause the block hashes to be requested
					ret.failed.emplace_back(
						piece_index_t{piece_O} + file_piece_offset, std::vector<int>());
				}
				else
				{
					ret.passed.push_back(piece_index_t{piece_O} + file_piece_offset);
					// record that these block hashes are correct!
					int const leafs_end = std::min(m_num_blocks, block_O + blocks_in_piece);
					// TODO: this could be done more efficiently if bitfield had a function
					// to set a range of bits
					for (int k = block_O; k < leafs_end; ++k)
						m_block_verified.set_bit(k);
				}
				TORRENT_ASSERT(piece_O >= 0);
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

		int const leaf_L = num_layers();
		if (blocks_verified(block_index, 1))
		{
			// if this blocks's hash is already known, check the passed-in hash against it
			if (compare_node(leaf_L, block_index, h))
				return std::make_tuple(set_block_result::ok, block_index, 1);
			else
				return std::make_tuple(set_block_result::block_hash_failed, block_index, 1);
		}

		allocate_compact();

		set(leaf_L, block_index, h);

		// to avoid wasting a lot of time hashing nodes only to discover they
		// cannot be verified, check first to see if the root of the largest
		// computable subtree is known

		auto const [leafs_start, leafs_size, root_L, root_O] =
			merkle_find_known_subtree(*this, block_index, m_num_blocks);

		// if the root node is unknown the hashes cannot be verified yet
		if (get(root_L, root_O).is_all_zeros())
			return std::make_tuple(set_block_result::unknown, leafs_start, leafs_size);

		// save the root hash because merkle_fill_tree will overwrite it
		sha256_hash const root = get(root_L, root_O);
		merkle_fill_tree(*this, leaf_L, leafs_start, leafs_size);

		if (root != get(root_L, root_O))
		{
			// hash failure, clear all the internal nodes
			// the whole piece failed the hash check. Clear all block hashes
			// in this piece and report a hash failure
			merkle_clear_tree(*this, leaf_L, leafs_start, leafs_size);
			set(root_L, root_O, root);
			return std::make_tuple(set_block_result::hash_failed, leafs_start, leafs_size);
		}

		// TODO: this could be done more efficiently if bitfield had a function
		// to set a range of bits
		int const leafs_end = std::min(m_num_blocks, leafs_start + leafs_size);
		for (int i = leafs_start; i < leafs_end; ++i)
			m_block_verified.set_bit(i);

		// attempting to optimize storage is quite costly, only do it if we have
		// a reason to believe it might have an effect
		if (block_index == m_num_blocks - 1 || !get(leaf_L, block_index + 1).is_all_zeros())
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

	bool merkle_tree::has_node(int const L, int const O) const
	{
		TORRENT_ASSERT(L >= 0);
		TORRENT_ASSERT(L <= num_layers());
		TORRENT_ASSERT(O >= 0);
		TORRENT_ASSERT(O < (1 << L));
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				TORRENT_ASSERT_FAIL();
				return false;
			case mode_t::empty_tree:
				return L == 0 && O == 0;
			case mode_t::full_tree:
				return !get(L, O).is_all_zeros();
			case mode_t::piece_layer:
				return L <= num_layers() - piece_levels();
			case mode_t::block_layer:
				return L < num_layers() || O < m_num_blocks;
		}
		TORRENT_ASSERT_FAIL();
		return false;
	}

	bool merkle_tree::compare_node(int const L, int const O, sha256_hash const& h) const
	{
		std::vector<sha256_hash> scratch;
		return get_impl(L, O, scratch) == h;
	}

	sha256_hash merkle_tree::node_at(int const L, int const O) const
	{
		std::vector<sha256_hash> scratch;
		return get_impl(L, O, scratch);
	}

	sha256_hash merkle_tree::get_impl(
		int const L, int const O, std::vector<sha256_hash>& scratch_space) const
	{
		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
				TORRENT_ASSERT_FAIL();
				return sha256_hash{};
			case mode_t::empty_tree:
				return (L == 0 && O == 0) ? root() : sha256_hash{};
			case mode_t::full_tree:
				return get(L, O);
			case mode_t::piece_layer:
			case mode_t::block_layer:
			{
				int const stored_L =
					(m_mode == mode_t::piece_layer) ? num_layers() - piece_levels() : num_layers();

				if (L > stored_L) return {};

				int const depth = stored_L - L;
				int const layer_size = 1 << depth;
				int const start_O = O << depth;

				if (start_O >= m_tree.end_index())
				{
					// the requested subtree is entirely in padding
					return merkle_pad(
						(m_mode == mode_t::piece_layer)
						? layer_size << m_blocks_per_piece_log
						: layer_size, 1);
				}

				sha256_hash const pad_hash = (m_mode == mode_t::piece_layer)
					? merkle_pad(1 << m_blocks_per_piece_log, 1)
					: sha256_hash{};
				auto const layer = span<sha256_hash const>(m_tree).subspan(
					start_O, std::min(m_tree.end_index() - start_O, layer_size));

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

		// produces the compact representation of the tree (the layout we
		// store in m_tree when in full_tree mode). For non-full modes,
		// place the known layer's hashes and walk upward, filling interior
		// nodes from the live entries plus the implied pad for missing
		// siblings.
		std::vector<sha256_hash> ret(std::size_t(m_layer_offsets.back()));
		int const N = num_layers();

		switch (m_mode)
		{
			case mode_t::uninitialized_tree:
			case mode_t::empty_tree:
				break;
			case mode_t::full_tree:
				ret.assign(m_tree.begin(), m_tree.end());
				break;
			case mode_t::piece_layer:
			case mode_t::block_layer:
			{
				// for small trees where the piece layer would be at or
				// above the root (a single-piece torrent with a large
				// piece size), treat the root as the piece layer
				int const start_L =
					m_mode == mode_t::piece_layer ? std::max(0, N - piece_levels()) : N;
				int const layer_start = m_layer_offsets[start_L];
				std::copy(m_tree.begin(), m_tree.end(), ret.begin() + layer_start);

				sha256_hash pad = m_mode == mode_t::piece_layer
					? merkle_pad(1 << m_blocks_per_piece_log, 1)
					: sha256_hash{};
				int level_L = start_L;
				int live = layer_size_live(level_L);
				while (level_L > 0)
				{
					int const parent_L = level_L - 1;
					int const parent_live = layer_size_live(parent_L);
					int const child_start = m_layer_offsets[level_L];
					int const parent_start = m_layer_offsets[parent_L];
					for (int O = 0; O < parent_live; ++O)
					{
						sha256_hash const left =
							2 * O < live ? ret[std::size_t(child_start + 2 * O)] : pad;
						sha256_hash const right =
							2 * O + 1 < live ? ret[std::size_t(child_start + 2 * O + 1)] : pad;
						ret[std::size_t(parent_start + O)] = hash_pair(left, right);
					}
					--level_L;
					live = parent_live;
					pad = hash_pair(pad, pad);
				}
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
				for (int i = 0, end = int(size()); i < end; ++i)
				{
					int const L = merkle_get_layer(i);
					sha256_hash const h = get(L, i - merkle_layer_start(L));
					if (h.is_all_zeros()) continue;
					ret.push_back(h);
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

	void merkle_tree::allocate_compact()
	{
		if (m_mode == mode_t::full_tree) return;

		INVARIANT_CHECK;

		// if we already have the complete tree, we shouldn't be allocating it
		// again.
		TORRENT_ASSERT(m_mode != mode_t::block_layer);
		m_tree = aux::vector<sha256_hash>(build_vector());
		m_mode = mode_t::full_tree;
		m_block_verified.resize(m_num_blocks, false);
	}

	int merkle_tree::num_layers() const { return int(m_layer_offsets.size()) - 2; }

	int merkle_tree::layer_size_live(int const level) const
	{
		TORRENT_ASSERT(level >= 0);
		TORRENT_ASSERT(level <= num_layers());
		// at the leaf level the live count is m_num_blocks; each step up
		// halves (rounded up) since pairs of leaves combine into a parent.
		int const shift = num_layers() - level;
		return (m_num_blocks + (1 << shift) - 1) >> shift;
	}

	bool merkle_tree::is_padding(int const level, int const offset) const
	{
		TORRENT_ASSERT(offset >= 0);
		return offset >= layer_size_live(level);
	}

	int merkle_tree::phys(int const level, int const offset) const
	{
		return m_layer_offsets[level] + offset;
	}

	sha256_hash merkle_tree::get(int const level, int const offset) const
	{
		TORRENT_ASSERT(m_mode == mode_t::full_tree);
		if (is_padding(level, offset)) return merkle_pad(1 << (num_layers() - level), 1);
		return m_tree[phys(level, offset)];
	}

	bool merkle_tree::set(int const level, int const offset, sha256_hash const& h)
	{
		TORRENT_ASSERT(m_mode == mode_t::full_tree);
		if (is_padding(level, offset))
		{
			// padding has an implied value; accept harmless writes (zero or
			// the implied pad) and reject anything else. This is the security
			// gate that turns a malicious uncle hash at a padding sibling
			// into a proof-validation failure.
			if (h.is_all_zeros()) return true;
			return h == merkle_pad(1 << (num_layers() - level), 1);
		}
		m_tree[phys(level, offset)] = h;
		return true;
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

		if (m_block_verified.all_set())
		{
			// extract the live leaf-layer slice from the compact storage
			int const leaf_start = m_layer_offsets[num_layers()];
			aux::vector<sha256_hash> new_tree(
				m_tree.begin() + leaf_start, m_tree.begin() + leaf_start + m_num_blocks);

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
		if (m_blocks_per_piece_log == 0) return;

		int const N = num_layers();
		// For trees small enough that the piece layer would be at or above
		// the root (single-piece torrents with a large piece size), treat
		// the root as the piece layer.
		int const piece_L = std::max(0, N - piece_levels());

		// validate the piece layer: each pair at piece_L must hash to its
		// parent at piece_L - 1. Walking live + padding entries; padding
		// is hashed from merkle_pad and the parent is the implied pad too,
		// so those pairs always pass. For a single-piece tree (piece_L == 0)
		// the piece layer is the root with no parent to validate against.
		if (piece_L > 0)
		{
			int const piece_layer_total = 1 << piece_L;
			for (int O = 0; O < piece_layer_total; O += 2)
			{
				// inlined merkle_validate_node so we reuse the per-tree
				// scratch hasher rather than constructing one per pair.
				if (hash_pair(get(piece_L, O), get(piece_L, O + 1)) != get(piece_L - 1, O / 2))
					return;
			}
		}

		// reject if there's any block-layer data we'd be dropping
		int const leaf_start = m_layer_offsets[N];
		int const leaf_end = m_layer_offsets[N + 1];
		if (!std::all_of(m_tree.begin() + leaf_start,
				m_tree.begin() + leaf_end,
				[](sha256_hash const& h) { return h.is_all_zeros(); }))
			return;

		int const piece_start = m_layer_offsets[piece_L];
		aux::vector<sha256_hash> new_tree(
			m_tree.begin() + piece_start, m_tree.begin() + piece_start + num_pieces());

		m_tree = std::move(new_tree);
		m_mode = mode_t::piece_layer;
		m_block_verified.clear();
	}

	std::vector<sha256_hash> merkle_tree::get_hashes(int const base
		, int const index, int const count, int const proof_layers) const
	{
		// given the full size of the tree, half of it, rounded up, are leaf nodes
		int const base_layer_idx = merkle_num_layers(num_leafs()) - base;

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
			for (int j = 0; j < count; ++j)
			{
				int const O = index + j;
				// the pad hashes are expected to be zero, they should not fail
				// the request
				if ((base != 0 || O < m_num_blocks) && !has_node(base_layer_idx, O)) return {};
				ret.push_back(get_impl(base_layer_idx, O, scratch_space));
			}
		}

		// the number of layers up the tree which can be computed from the base layer hashes
		// subtract one because the base layer doesn't count
		int const base_tree_layers = merkle_num_layers(merkle_num_leafs(count)) - 1;

		int proof_L = base_layer_idx;
		int proof_O = index;
		for (int i = 0; i < proof_layers; ++i)
		{
			--proof_L;
			proof_O >>= 1;

			// if this assert fire, the requester set proof_layers too high
			// and it wasn't correctly validated
			TORRENT_ASSERT(proof_L >= 0);

			if (i >= base_tree_layers)
			{
				int const sibling_O = proof_O ^ 1;
				if (!has_node(proof_L, proof_O) || !has_node(proof_L, sibling_O)) return {};

				ret.push_back(get_impl(proof_L, sibling_O, scratch_space));
			}
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
				TORRENT_ASSERT(get(0, 0) == root());
				TORRENT_ASSERT(m_block_verified.size() == m_num_blocks);

				int const leaf_L = num_layers();

				if (leaf_L == 0) break;

				// In all layers, except the block layer, all non-zero hashes
				// must have a non-zero sibling and they must validate with
				// their parent. Walks live (L, O) pairs only; padding pairs
				// are consistent by construction.
				for (int L = 1; L < leaf_L; ++L)
				{
					int const live = layer_size_live(L);
					for (int O = 0; O < live; O += 2)
					{
						if (get(L, O).is_all_zeros())
						{
							TORRENT_ASSERT(O + 1 >= live || get(L, O + 1).is_all_zeros());
							continue;
						}
						// inlined merkle_validate_node -- reuse the per-tree
						// scratch hasher across all pair checks.
						TORRENT_ASSERT(hash_pair(get(L, O), get(L, O + 1)) == get(L - 1, O / 2));
					}
				}

				// the block layer may contain invalid hashes, but if the
				// corresponding bit in m_block_verified is 1, they must be
				// correct, and match the block hashes.
				// validate all blocks (that can be validated)
				// since these are checked in pairs, we skip 2, to always
				// consider the left side of the pair
				for (int b = 0; b < m_num_blocks; b += 2)
				{
					if (b + 1 == m_num_blocks)
					{
						// the edge case where there's an odd number of blocks
						// and this is the last one
						if (!m_block_verified.get_bit(b)) continue;
						TORRENT_ASSERT(has_node(leaf_L, b));
						TORRENT_ASSERT(
							hash_pair(get(leaf_L, b), sha256_hash()) == get(leaf_L - 1, b / 2));
					}
					else
					{
						TORRENT_ASSERT(m_block_verified.get_bit(b) == m_block_verified.get_bit(b + 1));
						if (!m_block_verified.get_bit(b)) continue;
						TORRENT_ASSERT(has_node(leaf_L, b));
						TORRENT_ASSERT(has_node(leaf_L, b + 1));
						TORRENT_ASSERT(hash_pair(get(leaf_L, b), get(leaf_L, b + 1))
							== get(leaf_L - 1, b / 2));
					}
				}
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
