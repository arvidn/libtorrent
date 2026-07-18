/*

Copyright (c) 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_MERKLE_TREE_HPP_INCLUDED
#define TORRENT_MERKLE_TREE_HPP_INCLUDED

#include <cstdint>
#include <map>
#include <utility> // for pair
#include <tuple>
#include <optional>

#include "libtorrent/sha1_hash.hpp" // for sha256_hash
#include "libtorrent/aux_/debug.hpp" // for single_threaded
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/bitfield.hpp"
#if TORRENT_USE_INVARIANT_CHECKS
#include "libtorrent/aux_/invariant_check.hpp"
#endif

namespace libtorrent {
namespace aux {

struct add_hashes_result_t
{
	std::vector<piece_index_t> passed;
	std::vector<std::pair<piece_index_t, std::vector<int>>> failed;
};

// represents the merkle tree for files belonging to a torrent.
// Each file has a root-hash and a "piece layer", i.e. the level in the tree
// representing whole pieces. Those hashes are likely to be included in .torrent
// files and known up-front.

// The invariant of the tree is that all interior nodes (i.e. all but the very
// bottom leaf nodes, representing block hashes) are either set and valid, or
// clear. No invalid hashes are allowed, and they can only be added by also
// providing proof of being valid.

// The leaf blocks on the other hand, MAY be invalid. For instance, when adding
// a magnet link for a torrent that we already have files for. Once we have the
// metadata, we have files on disk but no hashes. We won't know whether the data
// on disk is valid or not, until we've downloaded the hashes to validate them.

// In full_tree mode, m_tree uses a compact, per-layer layout: only the
// *live* (non-padding) nodes of each layer are stored, packed in order:
// layer 0 (the root), then layer 1, ..., then the leaf layer.
// m_layer_offsets[L] is the start of layer L in m_tree;
// m_layer_offsets.back() is the total compact size. Padding nodes are not
// stored: get() returns the implied pad hash from the merkle_pad cache, and
// set() rejects writes of non-pad values to padding slots (the security
// gate that turns a malicious uncle hash at a padding sibling into a
// proof-validation failure).
struct TORRENT_EXTRA_EXPORT merkle_tree : private single_threaded
{
	// TODO: remove this constructor. Don't support "uninitialized" trees. This
	// also requires not constructing these for pad-files and small files as
	// well. So, a sparse hash list in torrent_info
	merkle_tree() = default;
	merkle_tree(int num_blocks, int blocks_per_piece, char const* r);

	sha256_hash root() const;

	// Hash a pair of child hashes using the per-tree scratch SHA-256
	// context. Reuses the underlying context rather than allocating a new
	// one each call -- the EVP_MD_CTX allocations add up in tight pair-
	// hashing loops. Asserted single-threaded; the merkle_tree is owned by
	// (and only ever touched from) the network thread.
	sha256_hash hash_pair(sha256_hash const& left, sha256_hash const& right) const
	{
		TORRENT_ASSERT(is_single_thread());
		m_scratch_hasher.reset();
		return m_scratch_hasher.update(left).update(right).final();
	}

	void load_tree(span<sha256_hash const> t, bitfield const& verified);
	void load_sparse_tree(span<sha256_hash const> t, bitfield const& mask
		, bitfield const& verified);
	void load_verified_bits(bitfield const& verified);

	std::size_t size() const;
	int end_index() const { return int(size()); }

	// Coordinate addressing for nodes in the tree. `level` 0 is the root,
	// `level == num_layers()` is the leaf (block) layer. `offset` is the
	// position within the layer, [0, layer_size_live(level)) for live
	// positions and [layer_size_live(level), 1 << level) for padding.
	int num_layers() const;
	int layer_size_live(int level) const;
	bool is_padding(int level, int offset) const;

	// Physical index into `m_tree` for the node at (level, offset). In
	// `full_tree` mode the underlying storage is the standard layout for
	// now; phase 5 swaps it for a compact, per-layer layout indexed by
	// `m_layer_offsets`.
	int phys(int level, int offset) const;

	// Read or write the node at (level, offset). `set` returns false if
	// the write is rejected (only relevant after the compact layout is
	// in place: writing a non-pad hash into a padding slot fails).
	sha256_hash get(int level, int offset) const;
	bool set(int level, int offset, sha256_hash const& h);

	bool has_node(int level, int offset) const;
	bool compare_node(int level, int offset, sha256_hash const& h) const;

	// Lazy-compute lookup for a (level, offset) node. Works in any mode,
	// computing interior nodes from a stored layer if needed.
	sha256_hash node_at(int level, int offset) const;

	// Transition into full_tree mode by allocating storage for all nodes
	// (compact in phase 5; standard layout currently). Idempotent on
	// full_tree; rejects block_layer (we'd lose verified block hashes).
	// Exposed so the merkle.cpp helpers and tests can address (L, O)
	// directly via get/set after construction.
	void allocate_compact();

	std::vector<sha256_hash> build_vector() const;
	std::pair<std::vector<sha256_hash>, bitfield> build_sparse_vector() const;

	// get bits indicating if each leaf hash is verified
	bitfield verified_leafs() const;

	// returns true if the entire tree is known and verified
	bool is_complete() const;

	// returns true if all block hashes in the specified range have been verified
	bool blocks_verified(int block_idx, int num_blocks) const;

	bool load_piece_layer(span<char const> piece_layer);

	// the leafs in "tree" must be block hashes (i.e. leaf hashes in the this
	// tree). This function inserts those hashes as well as the nodes up the
	// tree. The destination start index is the index, in this tree, to the first leaf
	// where "tree" will be inserted.
	// inserts the nodes in "proofs" as a path up the tree. The proofs are
	// sibling hashes, as they are returned from add_hashes(). The hashes must
	// be valid.
	// if the hashes are not valid, or the uncle hashes fail validation, nullopt
	// is returned.
	std::optional<add_hashes_result_t> add_hashes(
		int dest_start_idx
		, piece_index_t::diff_type file_piece_offset
		, span<sha256_hash const> hashes
		, span<sha256_hash const> uncle_hashes);

	aux::vector<sha256_hash> get_piece_layer() const;

	enum class set_block_result
	{
		ok, unknown, hash_failed, block_hash_failed
	};

	std::tuple<set_block_result, int, int> set_block(int block_index
		, sha256_hash const& h);

	std::vector<sha256_hash> get_hashes(int base
		, int index, int count, int proof_layers) const;

private:

	// set to an empty tree
	void clear();

	sha256_hash get_impl(int level, int offset, std::vector<sha256_hash>& scratch_space) const;

	int blocks_per_piece() const { return 1 << m_blocks_per_piece_log; }
	// the number tree levels per piece. This is 0 if the block layer is also
	// the piece layer.
	int piece_levels() const { return m_blocks_per_piece_log; }

	int block_layer_start() const;
	int piece_layer_start() const;
	int num_pieces() const;
	int num_leafs() const;

	void optimize_storage();
	void optimize_storage_piece_layer();

	// a pointer to the root hash for this file.
	char const* m_root = nullptr;

	// this is either the full tree, or some sparse representation of it,
	// depending on m_mode
	// TODO: make this a std::unique_ptr<sha256_hash[]>
	aux::vector<sha256_hash> m_tree;

	// physical start of each layer in `m_tree` when in `full_tree` mode.
	// size = num_layers + 2; the last entry holds the total physical size.
	// depends only on `m_num_blocks`; filled once in the constructor.
	aux::vector<std::int32_t> m_layer_offsets;

	// when the full tree is allocated, this has one bit for each block hash. a
	// 1 means we have verified the block hash to be correct, otherwise the block
	// hash may represent what's on disk, but we haven't been able to verify it
	// yet
	bitfield m_block_verified;

	// scratch SHA-256 context reused by hash_pair() for the per-tree
	// pair-hashing in build_vector, the merkle.cpp tree-walking helpers,
	// etc. mutable so const methods (build_vector, ...) can reset/update
	// it without giving up their const signature.
	mutable hasher256 m_scratch_hasher;

	// number of blocks in the file this tree represents. The number of leafs in
	// the tree is rounded up to an even power of 2.
	int m_num_blocks = 0;

	// the number of blocks per piece, specified as how many steps to shift
	// right 1 to get the number of blocks in one piece. This is a compact
	// representation that's valid because pieces are always powers of 2.
	// this is necessary to know which layer in the tree the piece layer is.
	std::uint8_t m_blocks_per_piece_log = 0;

	enum class mode_t : std::uint8_t
	{
		// a default constructed tree is truly empty. It does not even have a
		// root hash
		uninitialized_tree,

		// we don't have any hashes in this tree. m_tree should be empty
		// an empty tree still always have the root hash (available as root())
		empty_tree,

		// in this mode, m_tree represents the full tree, including padding.
		full_tree,

		// in this mode, m_tree represents the piece layer only, no padding
		// and all piece layer hashes are stored and valid
		piece_layer,

		// in this mode, m_tree represents the block (leaf) layer only, no padding
		// and all block layer hashes are stored and valid
		block_layer
	};
	mode_t m_mode = mode_t::uninitialized_tree;

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
	friend struct libtorrent::invariant_access;
#endif
};

}
}

#endif
