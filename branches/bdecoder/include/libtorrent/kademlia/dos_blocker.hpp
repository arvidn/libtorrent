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

#ifndef TORRENT_DHT_DOS_BLOCKER
#define TORRENT_DHT_DOS_BLOCKER

#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent { namespace dht
{

	// this is a class that maintains a list of abusive DHT nodes,
	// blocking their access to our DHT node.
	struct TORRENT_EXTRA_EXPORT dos_blocker
	{
		dos_blocker();

		// called every time we receive an incoming packet. Returns
		// true if we should let the packet through, and false if
		// it's blocked
		bool incoming(address addr, time_point now);

		void set_rate_limit(int l)
		{
			TORRENT_ASSERT(l > 0);
			m_message_rate_limit = l;
		}

		void set_block_timer(int t)
		{
			TORRENT_ASSERT(t > 0);
			m_block_timeout = t;
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

		enum { num_ban_nodes = 20 };

		// the max number of packets we can receive per second from a node before
		// we block it.
		int m_message_rate_limit;

		// the number of seconds a node gets blocked for when it exceeds the rate
		// limit
		int m_block_timeout;

		node_ban_entry m_ban_nodes[num_ban_nodes];
	};
}}

#endif

