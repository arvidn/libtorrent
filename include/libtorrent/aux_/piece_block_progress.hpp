/*

Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2005, 2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PIECE_BLOCK_PROGRESS_HPP_INCLUDED
#define TORRENT_PIECE_BLOCK_PROGRESS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/units.hpp"

namespace lt::aux {

	struct piece_block_progress
	{
		static inline constexpr piece_index_t invalid_index{-1};

		// the piece and block index
		// determines exactly which
		// part of the torrent that
		// is currently being downloaded
		piece_index_t piece_index{invalid_index};
		int block_index;
		// the number of bytes we have received
		// of this block
		int bytes_downloaded;
		// the number of bytes in the block
		int full_block_bytes;
	};
}

#endif // TORRENT_PIECE_BLOCK_PROGRESS_HPP_INCLUDED
