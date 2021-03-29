/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Mike Tzou
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_DHT_STATE_HPP
#define LIBTORRENT_DHT_STATE_HPP

#include <libtorrent/config.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/entry.hpp>

#include <libtorrent/kademlia/node_id.hpp>

#include <vector>
#include <utility>

namespace lt {

	struct bdecode_node;
}

namespace lt {
namespace dht {

	using node_ids_t = std::vector<std::pair<address, node_id>>;
	// This structure helps to store and load the state
	// of the ``dht_tracker``.
	// At this moment the library is only a dual stack
	// implementation of the DHT. See `BEP 32`_
	//
	// .. _`BEP 32`: https://www.bittorrent.org/beps/bep_0032.html
	struct TORRENT_EXPORT dht_state
	{
		node_ids_t nids;

		// the bootstrap nodes saved from the buckets node
		std::vector<udp::endpoint> nodes;
		// the bootstrap nodes saved from the IPv6 buckets node
		std::vector<udp::endpoint> nodes6;

		void clear();
	};

	TORRENT_EXTRA_EXPORT node_ids_t extract_node_ids(bdecode_node const& e, string_view key);
	TORRENT_EXTRA_EXPORT dht_state read_dht_state(bdecode_node const& e);
	TORRENT_EXTRA_EXPORT entry save_dht_state(dht_state const& state);
}
}

#endif // LIBTORRENT_DHT_STATE_HPP
