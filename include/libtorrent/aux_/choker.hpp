/*

Copyright (c) 2014, 2018-2020, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CHOKER_HPP_INCLUDED
#define TORRENT_CHOKER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp" // for time_duration
#include <vector>

namespace lt::aux {

	struct session_settings;
	struct peer_connection;

	// sorts the vector of peers in-place. When returning, the top unchoke slots
	// elements are the peers we should unchoke. This is similar to a partial
	// sort. Only the unchoke slots first elements are sorted.
	// the return value are the number of peers that should be unchoked. This
	// is also the number of elements that are valid at the beginning of the
	// peer list. Peers beyond this initial range are not sorted.
	TORRENT_EXTRA_EXPORT int unchoke_sort(std::vector<peer_connection*>& peers
		, time_duration unchoke_interval
		, aux::session_settings const& sett);

}

#endif // TORRENT_CHOKER_INCLUDED
