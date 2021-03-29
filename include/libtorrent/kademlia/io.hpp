/*

Copyright (c) 2016, Steven Siloti
Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef KADEMLIA_IO_HPP
#define KADEMLIA_IO_HPP

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/aux_/socket_io.hpp"

namespace lt::dht {

	struct node_endpoint
	{
		node_id id;
		udp::endpoint ep;
	};

	template<class InIt>
	node_endpoint read_node_endpoint(udp protocol, InIt&& in)
	{
		node_endpoint ep;
		std::copy(in, in + 20, ep.id.begin());
		in += 20;
		if (protocol == udp::v6())
			ep.ep = aux::read_v6_endpoint<udp::endpoint>(in);
		else
			ep.ep = aux::read_v4_endpoint<udp::endpoint>(in);
		return ep;
	}

}

#endif
