
/*

Copyright (c) 2015, Arvid Norberg
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

#ifndef TORRENT_SETUP_DHT_HPP_INCLUDED
#define TORRENT_SETUP_DHT_HPP_INCLUDED

#include <vector>
#include "libtorrent/kademlia/dht_settings.hpp" // for dht_settings
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_types.hpp" // for dht_routing_bucket

namespace sim
{
	struct simulation;
}

struct dht_node;

void print_routing_table(std::vector<lt::dht_routing_bucket> const& rt);

struct dht_network
{
	enum flags_t
	{
		add_dead_nodes = 1,
		bind_ipv6 = 2
	};

	dht_network(sim::simulation& sim, int num_nodes, std::uint32_t flags = 0);
	~dht_network();

	void stop();
	std::vector<lt::udp::endpoint> router_nodes() const;

private:

	// used for all the nodes in the network
	lt::counters m_cnt;
	lt::dht::settings m_sett;
	std::list<dht_node> m_nodes;
};

#endif

