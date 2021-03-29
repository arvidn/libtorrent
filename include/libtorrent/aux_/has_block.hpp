/*

Copyright (c) 2016-2017, 2020, Arvid Norberg
Copyright (c) 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_HAS_BLOCK_HPP_INCLUDED
#define TORRENT_HAS_BLOCK_HPP_INCLUDED

#include "libtorrent/piece_block.hpp"
#include "libtorrent/aux_/peer_connection.hpp" // for pending_block

namespace lt::aux {

	struct has_block
	{
		has_block(has_block const&) = default;
		// explicitly disallow assignment, to silence msvc warning
		has_block& operator=(has_block const&) = delete;

		explicit has_block(piece_block const& b): block(b) {}
		bool operator()(pending_block const& pb) const
		{ return pb.block == block; }
	private:
		piece_block const& block;
	};

}

#endif
