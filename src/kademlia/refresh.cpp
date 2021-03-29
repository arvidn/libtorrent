/*

Copyright (c) 2006, Daniel Wallin
Copyright (c) 2006, 2008-2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/performance_counters.hpp>

namespace lt { namespace dht {

observer_ptr bootstrap::new_observer(udp::endpoint const& ep
	, node_id const& id)
{
	auto o = m_node.m_rpc.allocate_observer<get_peers_observer>(self(), ep, id);
#if TORRENT_USE_ASSERTS
	if (o) o->m_in_constructor = false;
#endif
	return o;
}

bool bootstrap::invoke(observer_ptr o)
{
	entry e;
	e["y"] = "q";
	entry& a = e["a"];

	e["q"] = "get_peers";
	// in case our node id changes during the bootstrap, make sure to always use
	// the current node id (rather than the target stored in the traversal
	// algorithm)
	node_id target = get_node().nid();
	make_id_secret(target);
	a["info_hash"] = target.to_string();

	if (o->flags & observer::flag_initial)
	{
		// if this packet is being sent to a bootstrap/router node, let it know
		// that we're actually bootstrapping (as opposed to being collateral
		// traffic).
		a["bs"] = 1;
	}

//	e["q"] = "find_node";
//	a["target"] = target.to_string();
	m_node.stats_counters().inc_stats_counter(counters::dht_get_peers_out);
	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

bootstrap::bootstrap(
	node& dht_node
	, node_id const& target
	, done_callback const& callback)
	: get_peers(dht_node, target, get_peers::data_callback(), callback, false)
{
}

char const* bootstrap::name() const { return "bootstrap"; }

void bootstrap::done()
{
#ifndef TORRENT_DISABLE_LOGGING
	get_node().observer()->log(dht_logger::traversal, "[%u] bootstrap done, pinging remaining nodes"
		, id());
#endif

	for (auto const& o : m_results)
	{
		if (o->flags & observer::flag_queried) continue;
		// this will send a ping
		m_node.add_node(o->target_ep());
	}
	get_peers::done();
}

} } // namespace lt::dht
