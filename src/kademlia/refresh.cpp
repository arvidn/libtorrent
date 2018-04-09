/*

Copyright (c) 2006-2018, Arvid Norberg & Daniel Wallin
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

#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/performance_counters.hpp>

#include <libtorrent/io.hpp>

namespace libtorrent { namespace dht
{

observer_ptr bootstrap::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) get_peers_observer(this, ep, id));
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
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
		// that we're actualy bootstrapping (as opposed to being collateral
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
	, node_id target
	, done_callback const& callback)
	: get_peers(dht_node, target, get_peers::data_callback(), callback, false)
{
}

char const* bootstrap::name() const { return "bootstrap"; }

void bootstrap::done()
{
#ifndef TORRENT_DISABLE_LOGGING
	get_node().observer()->log(dht_logger::traversal, "[%p] bootstrap done, pinging remaining nodes"
		, static_cast<void*>(this));
#endif

	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end; ++i)
	{
		if ((*i)->flags & observer::flag_queried) continue;
		// this will send a ping
		m_node.add_node((*i)->target_ep());
	}
	get_peers::done();
}

} } // namespace libtorrent::dht

