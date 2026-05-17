/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/precomputed_block_hashes.hpp"
#include "libtorrent/assert.hpp"

#include <algorithm>

namespace libtorrent::aux {

	void precomputed_block_hashes::store(
		piece_index_t const piece, int const block, int const num_blocks, sha256_hash const& h)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		auto& entry = m_hashes[piece];
		// newly created elements are value-initialized to all-zero, i.e. "not
		// computed".
		if (entry.empty()) entry.resize(num_blocks);
		TORRENT_ASSERT(block >= 0 && block < num_blocks);
		// a real block hash is never all zeros (that's the "not computed" sentinel)
		TORRENT_ASSERT(!h.is_all_zeros());
		entry[block] = h;
	}

	aux::vector<sha256_hash> precomputed_block_hashes::take(piece_index_t const piece)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		auto const it = m_hashes.find(piece);
		if (it == m_hashes.end()) return {};
		aux::vector<sha256_hash> ret = std::move(it->second);
		m_hashes.erase(it);
		return ret;
	}

	std::optional<sha256_hash> precomputed_block_hashes::take_block(
		piece_index_t const piece, int const block)
	{
		TORRENT_ASSERT(block >= 0);
		std::lock_guard<std::mutex> l(m_mutex);
		auto const it = m_hashes.find(piece);
		if (it == m_hashes.end()) return std::nullopt;
		auto& entry = it->second;
		if (block >= int(entry.size()) || entry[block].is_all_zeros()) return std::nullopt;
		sha256_hash const h = entry[block];
		// mark the block consumed; drop the piece once nothing computed remains
		entry[block].clear();
		bool const any = std::any_of(
			entry.begin(), entry.end(), [](sha256_hash const& x) { return !x.is_all_zeros(); });
		if (!any) m_hashes.erase(it);
		return h;
	}

	void precomputed_block_hashes::drop(piece_index_t const piece)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_hashes.erase(piece);
	}

}
