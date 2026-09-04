/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_MERGE_BLOCK_REQUESTS_HPP_INCLUDED
#define TORRENT_MERGE_BLOCK_REQUESTS_HPP_INCLUDED

#include "libtorrent/piece_block.hpp"
#include "libtorrent/assert.hpp"

#include <algorithm>
#include <cstdint>

namespace libtorrent::aux {

// accumulates a run of flat-contiguous blocks into a single byte-range
// request, tracking the torrent-space byte offset where the accumulated
// range currently ends. Constructed with the first block of the run
// already merged in; merge() extends it one block at a time.
struct request_merger
{
	request_merger(piece_block const first,
		int const first_piece_size,
		int const piece_length,
		int const block_size)
		: m_piece_length(piece_length)
		, m_block_size(block_size)
		, m_piece_size(first_piece_size)
		, m_end(start_offset(first)
			  + std::min(first_piece_size - first.block_index * block_size, block_size))
	{}

	// true if "next" starts exactly where the accumulated range currently
	// ends. This also rejects a "next" that starts a new piece right after
	// a truncated one (piece_size() < piece_length, only possible at a file
	// boundary in a v2-only torrent): the truncated piece's real bytes stop
	// short of the next piece's nominal start, so the offsets won't match.
	// Use this to skip an expensive piece_size_for_req() lookup before
	// calling merge() when it wouldn't merge anyway.
	bool continues(piece_block const next) const { return start_offset(next) == m_end; }

	// requires continues(next). Merges "next" in and returns the (always
	// positive) number of bytes it contributes.
	//
	// next_piece_size is piece_size_for_req(next.piece_index); pass
	// piece_size() through when next shares a piece with the last block.
	int merge(piece_block const next, int const next_piece_size)
	{
		TORRENT_ASSERT_PRECOND(continues(next));

		int const block_offset = next.block_index * m_block_size;
		int const bs = std::min(next_piece_size - block_offset, m_block_size);
		TORRENT_ASSERT(bs > 0);
		TORRENT_ASSERT(bs <= m_block_size);

		m_piece_size = next_piece_size;
		m_end += bs;
		return bs;
	}

	int piece_size() const { return m_piece_size; }

private:
	std::int64_t start_offset(piece_block const b) const
	{
		return std::int64_t(static_cast<int>(b.piece_index)) * m_piece_length
			+ std::int64_t(b.block_index) * m_block_size;
	}

	// the nominal, fixed piece length for this torrent
	int m_piece_length;
	int m_block_size;
	// piece_size_for_req() of the last piece merged in, possibly truncated
	int m_piece_size;
	std::int64_t m_end;
};

}

#endif
