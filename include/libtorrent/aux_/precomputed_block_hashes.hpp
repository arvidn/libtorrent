/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PRECOMPUTED_BLOCK_HASHES_HPP
#define TORRENT_PRECOMPUTED_BLOCK_HASHES_HPP

#include <unordered_map>
#include <mutex>
#include <optional>

#include "libtorrent/units.hpp" // for piece_index_t
#include "libtorrent/sha1_hash.hpp" // for sha256_hash
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/export.hpp" // for TORRENT_EXTRA_EXPORT

namespace libtorrent::aux {

	// A thread-safe cache of SHA-256 block hashes computed by the v2 hash
	// queue (disk_cache::drain_v2_hash_queue) while the block buffer is still
	// in memory, so a later hash/hash2 job doesn't have to re-read the block
	// from disk just to hash it. Keyed by piece, then indexed by block within
	// the piece.
	//
	// A block whose hash has not been computed reads as an all-zero hash. A
	// SHA-256 over a (non-empty) block is never all zeros, so this doubles as the
	// "not present" sentinel and avoids a separate bitfield.
	struct TORRENT_EXTRA_EXPORT precomputed_block_hashes
	{
		// store the SHA-256 of a single block. num_blocks is the number of blocks
		// in the piece; it sizes the per-piece array the first time a block of the
		// piece is stored.
		void store(piece_index_t piece, int block, int num_blocks, sha256_hash const& h);

		// remove and return all of a piece's block hashes (indexed by block).
		// Returns an empty vector if the piece has none. Blocks that were never
		// stored read as all-zero.
		aux::vector<sha256_hash> take(piece_index_t piece);

		// remove and return a single block's hash, or nullopt if it wasn't stored.
		std::optional<sha256_hash> take_block(piece_index_t piece, int block);

		// discard all of a piece's hashes (e.g. on clear_piece / re-download).
		void drop(piece_index_t piece);

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<piece_index_t, aux::vector<sha256_hash>> m_hashes;
	};

}

#endif
