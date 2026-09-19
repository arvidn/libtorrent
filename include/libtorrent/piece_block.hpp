/*

Copyright (c) 2016-2017, 2020, 2022, Arvid Norberg
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PIECE_BLOCK_HPP_INCLUDED
#define TORRENT_PIECE_BLOCK_HPP_INCLUDED

#include <cstdint>

#include "libtorrent/units.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent {

	// identifies a single block within a torrent. ``piece_index`` is the
	// piece the block is part of and ``block_index`` is the index of the
	// block within the piece. Blocks are normally 16 KiB.
	struct TORRENT_EXPORT piece_block
	{
		// sentinel value representing "no block". Compares unequal to any
		// real block.
		static const piece_block invalid;

		piece_block() = default;
		piece_block(piece_index_t p_index, int b_index)
			: piece_index(p_index)
			, block_index(b_index)
		{
		}
		piece_index_t piece_index {0};
		int block_index = 0;

		bool operator<(piece_block const& b) const
		{
			if (piece_index < b.piece_index) return true;
			if (piece_index == b.piece_index) return block_index < b.block_index;
			return false;
		}

		bool operator==(piece_block const& b) const
		{ return piece_index == b.piece_index && block_index == b.block_index; }

		bool operator!=(piece_block const& b) const
		{ return piece_index != b.piece_index || block_index != b.block_index; }
	};

	namespace aux {

	// the byte offset, within the whole torrent, of "offset" bytes into "piece"
	constexpr std::int64_t torrent_byte_offset(
		piece_index_t const piece, std::int64_t const offset, int const piece_length) noexcept
	{
		return static_cast<std::int64_t>(static_cast<int>(piece)) * piece_length + offset;
	}

	// the byte offset, within the whole torrent, of block "b"
	constexpr std::int64_t torrent_byte_offset(
		piece_block const b, int const piece_length, int const block_size) noexcept
	{
		TORRENT_ASSERT(piece_length >= block_size);
		TORRENT_ASSERT(std::int64_t(b.block_index) * block_size <= piece_length);
		return torrent_byte_offset(
			b.piece_index, std::int64_t(b.block_index) * block_size, piece_length);
	}
	}
}
#endif
