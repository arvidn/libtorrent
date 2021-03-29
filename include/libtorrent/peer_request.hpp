/*

Copyright (c) 2004, 2009, 2013-2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_REQUEST_HPP_INCLUDED
#define TORRENT_PEER_REQUEST_HPP_INCLUDED

#include "libtorrent/units.hpp"

namespace lt {

	// represents a byte range within a piece. Internally this is is used for
	// incoming piece requests.
	struct TORRENT_EXPORT peer_request
	{
		// The index of the piece in which the range starts.
		piece_index_t piece;
		// The byte offset within that piece where the range starts.
		int start;
		// The size of the range, in bytes.
		int length;

		// returns true if the right hand side peer_request refers to the same
		// range as this does.
		bool operator==(peer_request const& r) const
		{ return piece == r.piece && start == r.start && length == r.length; }
	};
}

#endif // TORRENT_PEER_REQUEST_HPP_INCLUDED
