/*

Copyright (c) 2006-2014, Arvid Norberg & Daniel Wallin
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
#include <libtorrent/performance_counters.hpp>

#include <libtorrent/io.hpp>

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(traversal);
#endif

refresh::refresh(
	node_impl& node
	, node_id target
	, done_callback const& callback)
	: get_peers(node, target, get_peers::data_callback(), callback, false)
{
}

char const* refresh::name() const
{
	return "refresh";
}

observer_ptr refresh::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) get_peers_observer(this, ep, id));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

bool refresh::invoke(observer_ptr o)
{
	entry e;
	e["y"] = "q";
	e["q"] = "find_node";
	entry& a = e["a"];
	a["target"] = target().to_string();
	m_node.stats_counters().inc_stats_counter(counters::dht_find_node_out);
	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

bootstrap::bootstrap(
	node_impl& node
	, node_id target
	, done_callback const& callback)
	: refresh(node, target, callback)
{
}

char const* bootstrap::name() const { return "bootstrap"; }

void bootstrap::done()
{
	// TODO: 4 when bootstrapping against our own IP completes,
	// continue to issue another bootstrap against the deepest,
	// non-full bucket. when it completes, issue a bootstrap against
	// one bucket above it, and so on until the bootstrap lookup
	// against the top level bucket (bucket 0) completes. That's
	// when the bootstrap is done

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << "[" << this << "]"
		<< " bootstrap done, pinging remaining nodes";
#endif

	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end; ++i)
	{
		if ((*i)->flags & observer::flag_queried) continue;
		// this will send a ping
		m_node.add_node((*i)->target_ep());
	}
	refresh::done();
}

} } // namespace libtorrent::dht

