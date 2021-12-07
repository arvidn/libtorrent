/*

Copyright (c) 2015-2019, 2021, Arvid Norberg
Copyright (c) 2016, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SETUP_DHT_HPP_INCLUDED
#define TORRENT_SETUP_DHT_HPP_INCLUDED

#include <vector>
#include "libtorrent/aux_/session_settings.hpp"
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
	lt::aux::session_settings m_sett;
	std::list<dht_node> m_nodes;
};

#endif

