/*

Copyright (c) 2020-2021, Arvid Norberg
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

#ifndef TORRENT_MERKLE_TREE_HPP_INCLUDED
#define TORRENT_MERKLE_TREE_HPP_INCLUDED

#include <cstdint>
#include <map>
#include <utility> // for pair
#include <tuple>

#include "libtorrent/sha1_hash.hpp" // for sha256_hash
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/optional.hpp"
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

// Idea for future space optimization:
// while downloading, we need to store interior nodes of this tree. However, we
// don't need to store the padding. a SHA-256 is 32 bytes. Instead of storing
// the full (padded) tree of SHA-256 hashes, store the full tree of 32 bit
// signed integers, being indices into the actual storage for the tree. We could
// even grow the storage lazily. Instead of storing the padding hashes, use
// negative indices to refer to fixed SHA-256(0), and SHA-256(SHA-256(0)) and so
// on
struct TORRENT_EXTRA_EXPORT merkle_tree
{
	// TODO: remove this constructor. Don't support "uninitialized" trees. This
	// also requires not constructing these for pad-files and small files as
	// well. So, a sparse hash list in torrent_info
	merkle_tree() = default;
	merkle_tree(int num_blocks, int blocks_per_piece, char const* r);

	sha256_hash root() const;

	void load_tree(span<sha256_hash const> t, std::vector<bool> const& verified);
	void load_sparse_tree(span<sha256_hash const> t, std::vector<bool> const& mask
		, std::vector<bool> const& verified);
	void load_verified_bits(std::vector<bool> const& verified);

	std::size_t size() const;
	int end_index() const { return int(size()); }

	bool has_node(int idx) const;

	bool compare_node(int idx, sha256_hash const& h) const;

	sha256_hash operator[](int idx) const;

	std::vector<sha256_hash> build_vector() const;
	std::pair<std::vector<sha256_hash>, aux::vector<bool>> build_sparse_vector() const;

	// get bits indicating if each leaf hash is verified
	std::vector<bool> verified_leafs() const;

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
	boost::optional<add_hashes_result_t> add_hashes(
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

	sha256_hash get_impl(int idx, std::vector<sha256_hash>& scratch_space) const;

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
	void allocate_full();

	// a pointer to the root hash for this file.
	char const* m_root = nullptr;

	// this is either the full tree, or some sparse representation of it,
	// depending on m_mode
	// TODO: make this a std::unique_ptr<sha256_hash[]>
	aux::vector<sha256_hash> m_tree;

	// when the full tree is allocated, this has one bit for each block hash. a
	// 1 means we have verified the block hash to be correct, otherwise the block
	// hash may represent what's on disk, but we haven't been able to verify it
	// yet
	bitfield m_block_verified;

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
