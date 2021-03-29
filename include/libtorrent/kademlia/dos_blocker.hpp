/*

Copyright (c) 2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DHT_DOS_BLOCKER
#define TORRENT_DHT_DOS_BLOCKER

#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/assert.hpp"

namespace lt {
namespace dht {

	struct dht_logger;

	// this is a class that maintains a list of abusive DHT nodes,
	// blocking their access to our DHT node.
	struct TORRENT_EXTRA_EXPORT dos_blocker
	{
		dos_blocker();

		// called every time we receive an incoming packet. Returns
		// true if we should let the packet through, and false if
		// it's blocked
		bool incoming(address const& addr, time_point now, dht_logger* logger);

		void set_rate_limit(int l)
		{
			m_message_rate_limit = std::max(1, l);
		}

		void set_block_timer(int t)
		{
			m_block_timeout = std::max(1, t);
		}

	private:

		// used to ignore abusive dht nodes
		struct node_ban_entry
		{
			node_ban_entry(): count(0) {}
			address src;
			time_point limit;
			int count;
		};

		static constexpr int num_ban_nodes = 20;

		// the max number of packets we can receive per second from a node before
		// we block it.
		int m_message_rate_limit;

		// the number of seconds a node gets blocked for when it exceeds the rate
		// limit
		int m_block_timeout;

		node_ban_entry m_ban_nodes[num_ban_nodes];
	};
}
}

#endif
