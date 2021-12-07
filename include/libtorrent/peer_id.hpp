/*

Copyright (c) 2003, 2009, 2013, 2016-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_ID_HPP_INCLUDED
#define TORRENT_PEER_ID_HPP_INCLUDED

#include "libtorrent/sha1_hash.hpp"

namespace libtorrent {

	using peer_id = sha1_hash;
}

#endif // TORRENT_PEER_ID_HPP_INCLUDED
