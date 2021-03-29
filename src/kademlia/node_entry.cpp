/*

Copyright (c) 2015, Steven Siloti
Copyright (c) 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/kademlia/node_entry.hpp"
#include "libtorrent/aux_/time.hpp" // for aux::time_now()

namespace lt { namespace dht {

	node_entry::node_entry(node_id const& id_, udp::endpoint const& ep
		, int roundtriptime
		, bool pinged)
		: last_queried(pinged ? aux::time_now() : min_time())
		, id(id_)
		, endpoint(ep)
		, rtt(roundtriptime & 0xffff)
		, timeout_count(pinged ? 0 : 0xff)
		, verified(verify_id(id_, ep.address()))
	{
	}

	node_entry::node_entry(udp::endpoint const& ep)
		: endpoint(ep)
	{}

	void node_entry::update_rtt(int const new_rtt)
	{
		TORRENT_ASSERT(new_rtt <= 0xffff);
		TORRENT_ASSERT(new_rtt >= 0);
		if (new_rtt == 0xffff) return;
		if (rtt == 0xffff) rtt = std::uint16_t(new_rtt);
		else rtt = std::uint16_t(int(rtt) * 2 / 3 + new_rtt / 3);
	}

}}
