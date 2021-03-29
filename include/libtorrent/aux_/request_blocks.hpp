/*

Copyright (c) 2010, 2014-2015, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_REQUEST_BLOCKS_HPP_INCLUDED
#define TORRENT_REQUEST_BLOCKS_HPP_INCLUDED

#include "libtorrent/peer_info.hpp"

namespace lt::aux {

	struct peer_connection;
	struct torrent;

	// returns false if the piece picker was not invoked, because
	// of an early exit condition. In this case, the stats counter
	// shouldn't be incremented, since it won't use any significant
	// amount of CPU
	bool request_a_block(torrent& t, peer_connection& c);

	// returns the rank of a peer's source. We have an affinity
	// to connecting to peers with higher rank. This is to avoid
	// problems when our peer list is diluted by stale peers from
	// the resume data for instance
	int source_rank(peer_source_flags_t source_bitmask);
}

#endif
