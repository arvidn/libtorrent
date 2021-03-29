/*

Copyright (c) 2010, 2014-2020, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/kademlia/dos_blocker.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/aux_/socket_io.hpp" // for print_address
#include "libtorrent/kademlia/dht_observer.hpp" // for dht_logger
#endif

namespace lt::dht {

	dos_blocker::dos_blocker()
		: m_message_rate_limit(5)
		, m_block_timeout(5 * 60)
	{
		for (auto& e : m_ban_nodes)
		{
			e.count = 0;
			e.limit = min_time();
		}
	}

	bool dos_blocker::incoming(address const& addr, time_point const now, dht_logger* logger)
	{
		TORRENT_UNUSED(logger);
		node_ban_entry* match = nullptr;
		node_ban_entry* min = m_ban_nodes;
		for (node_ban_entry* i = m_ban_nodes; i < m_ban_nodes + num_ban_nodes; ++i)
		{
			if (i->src == addr)
			{
				match = i;
				break;
			}
			if (i->count < min->count) min = i;
			else if (i->count == min->count
				&& i->limit < min->limit) min = i;
		}

		if (match)
		{
			++match->count;

			if (match->count >= m_message_rate_limit * 10)
			{
				if (now < match->limit)
				{
					if (match->count == m_message_rate_limit * 10)
					{
#ifndef TORRENT_DISABLE_LOGGING
						if (logger != nullptr && logger->should_log(dht_logger::tracker))
						{
							logger->log(dht_logger::tracker, "BANNING PEER [ ip: %s time: %d ms count: %d ]"
								, aux::print_address(addr).c_str()
								, int(total_milliseconds((now - match->limit) + seconds(10)))
								, match->count);
						}
#else
						TORRENT_UNUSED(logger);
#endif // TORRENT_DISABLE_LOGGING
						// we've received too many messages in less than 10 seconds
						// from this node. Ignore it until it's silent for 5 minutes
						match->limit = now + seconds(m_block_timeout);
					}

					return false;
				}

				// the messages we received from this peer took more than 10
				// seconds. Reset the counter and the timer
				match->count = 0;
				match->limit = now + seconds(10);
			}
		}
		else
		{
			min->count = 1;
			min->limit = now + seconds(10);
			min->src = addr;
		}
		return true;
	}
}
