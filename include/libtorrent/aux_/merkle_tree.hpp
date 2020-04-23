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
	merkle_tree(int num_blocks, char const* r);

	sha256_hash root() const;

	void load_tree(span<sha256_hash const> t);

	std::size_t size() const;
	int end_index() const { return int(size()); }

	sha256_hash& operator[](int const idx) { return m_tree[idx]; }
	sha256_hash const& operator[](int const idx) const { return m_tree[idx]; }

	std::vector<sha256_hash> build_vector() const;

	void fill(int piece_layer_size);
	void fill(int piece_layer_size, int level_start);
	void clear(int num_leafs, int level_start);

	bool load_piece_layer(span<char const> piece_layer);

private:
	char const* m_root = nullptr;
	aux::vector<sha256_hash> m_tree;
	int m_num_blocks = 0;
};

}
}

#endif
