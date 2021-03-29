/*

Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PIECE_BLOCK_HPP_INCLUDED
#define TORRENT_PIECE_BLOCK_HPP_INCLUDED

#include "libtorrent/units.hpp"

namespace lt {

	struct TORRENT_EXPORT piece_block
	{
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
}
#endif
