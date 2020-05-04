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

	std::size_t merkle_tree::size() const
	{
		return static_cast<std::size_t>(merkle_num_nodes(merkle_num_leafs(m_num_blocks)));
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
