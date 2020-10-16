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

namespace libtorrent {
namespace aux {

// represents the merkle tree for files belonging to a torrent.
// Each file has a root-hash and a "piece layer", i.e. the level in the tree
// representing whole pieces. Those hashes are likely to be included in .torrent
// files and known up-front.

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

	void load_tree(span<sha256_hash const> t);
	void load_sparse_tree(span<sha256_hash const> t, std::vector<bool> const& mask);

	std::size_t size() const;
	int end_index() const { return int(size()); }

	bool has_node(int idx) const;

	bool compare_node(int idx, sha256_hash const& h) const;

	sha256_hash operator[](int idx) const;

	std::vector<sha256_hash> build_vector() const;
	std::pair<std::vector<sha256_hash>, aux::vector<bool>> build_sparse_vector() const;

	bool load_piece_layer(span<char const> piece_layer);

	// the leafs in "tree" must be block hashes (i.e. leaf hashes in the this
	// tree). This function inserts those hashes as well as the nodes up the
	// tree. The destination start index is the index, in this tree, to the first leaf
	// where "tree" will be inserted.
	std::map<piece_index_t, std::vector<int>> add_hashes(
		int dest_start_idx, span<sha256_hash const> tree);

	// inserts the nodes in "proofs" as a path up the tree starting at
	// "dest_start_idx". The proofs are sibling hashes, as they are returned
	// from add_hashes(). The hashes must be valid.
	void add_proofs(int dest_start_idx
		, span<std::pair<sha256_hash, sha256_hash> const> proofs);

	// returns the index of the pieces that passed the hash check
	std::vector<piece_index_t> check_pieces(int base
		, int index, int file_piece_offset
		, span<sha256_hash const> hashes);

	aux::vector<sha256_hash> get_piece_layer() const;

	enum class set_block_result
	{
		ok, unknown, hash_failed
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
};

}
}

#endif
