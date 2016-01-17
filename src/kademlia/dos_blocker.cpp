/*

Copyright (c) 2006-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/kademlia/dos_blocker.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/socket_io.hpp" // for print_address
#include "libtorrent/kademlia/dht_observer.hpp" // for dht_logger
#endif

namespace libtorrent { namespace dht
{
	dos_blocker::dos_blocker()
		: m_message_rate_limit(5)
		, m_block_timeout(5 * 60)
	{
		for (int i = 0; i < num_ban_nodes; ++i)
		{
			m_ban_nodes[i].count = 0;
			m_ban_nodes[i].limit = min_time();
		}
	}

	bool dos_blocker::incoming(address addr, time_point now, dht_logger* logger)
	{
		node_ban_entry* match = 0;
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
						logger->log(dht_logger::tracker, "BANNING PEER [ ip: %s time: %f count: %d ]"
							, print_address(addr).c_str()
							, total_milliseconds((now - match->limit) + seconds(10)) / 1000.f
							, int(match->count));
#endif
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
}}

