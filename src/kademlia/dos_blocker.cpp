/*

Copyright (c) 2006-2013, Arvid Norberg
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

#ifdef TORRENT_DHT_VERBOSE_LOGGING
#include "libtorrent/kademlia/logging.hpp"
#endif

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(dht_tracker);
#endif

	dos_blocker::dos_blocker() {}

	bool dos_blocker::incoming(address addr, ptime now)
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
		}

		if (match)
		{
			++match->count;
			if (match->count >= 20)
			{
				if (now < match->limit)
				{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					if (match->count == 20)
					{
						TORRENT_LOG(dht_tracker) << " BANNING PEER [ ip: "
							<< addr << " time: " << total_milliseconds((now - match->limit) + seconds(5)) / 1000.f
							<< " count: " << match->count << " ]";
					}
#endif
		
					// we've received 20 messages in less than 5 seconds from
					// this node. Ignore it until it's silent for 5 minutes
					match->limit = now + minutes(5);
					return false;
				}

				// we got 50 messages from this peer, but it was in
				// more than 5 seconds. Reset the counter and the timer
				match->count = 0;
				match->limit = now + seconds(5);
			}
		}
		else
		{
			min->count = 1;
			min->limit = now + seconds(5);
			min->src = addr;
		}
		return true;
	}
}
}

