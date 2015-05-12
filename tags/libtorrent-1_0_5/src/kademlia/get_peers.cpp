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

#include <libtorrent/kademlia/get_peers.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/socket_io.hpp>

namespace libtorrent { namespace dht
{

using detail::read_endpoint_list;
using detail::read_v4_endpoint;
#if TORRENT_USE_IPV6
using detail::read_v6_endpoint;
#endif

void get_peers_observer::reply(msg const& m)
{
	lazy_entry const* r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get() << "] missing response dict";
#endif
		return;
	}

	// look for peers
	lazy_entry const* n = r->dict_find_list("values");
	if (n)
	{
		std::vector<tcp::endpoint> peer_list;
		if (n->list_size() == 1 && n->list_at(0)->type() == lazy_entry::string_t)
		{
			// assume it's mainline format
			char const* peers = n->list_at(0)->string_ptr();
			char const* end = peers + n->list_at(0)->string_length();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			lazy_entry const* id = r->dict_find_string("id");
			if (id && id->string_length() == 20)
			{
				TORRENT_LOG(traversal)
					<< "[" << m_algorithm.get() << "] PEERS"
					<< " invoke-count: " << m_algorithm->invoke_count()
					<< " branch-factor: " << m_algorithm->branch_factor()
					<< " addr: " << m.addr
					<< " id: " << node_id(id->string_ptr())
					<< " distance: " << distance_exp(m_algorithm->target(), node_id(id->string_ptr()))
					<< " p: " << ((end - peers) / 6);
			}
#endif
			while (end - peers >= 6)
				peer_list.push_back(read_v4_endpoint<tcp::endpoint>(peers));
		}
		else
		{
			// assume it's uTorrent/libtorrent format
			read_endpoint_list<tcp::endpoint>(n, peer_list);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			lazy_entry const* id = r->dict_find_string("id");
			if (id && id->string_length() == 20)
			{
				TORRENT_LOG(traversal)
					<< "[" << m_algorithm.get() << "] PEERS"
					<< " invoke-count: " << m_algorithm->invoke_count()
					<< " branch-factor: " << m_algorithm->branch_factor()
					<< " addr: " << m.addr
					<< " id: " << node_id(id->string_ptr())
					<< " distance: " << distance_exp(m_algorithm->target(), node_id(id->string_ptr()))
					<< " p: " << n->list_size();
			}
#endif
		}
		static_cast<get_peers*>(m_algorithm.get())->got_peers(peer_list);
	}

	find_data_observer::reply(m);
}

void get_peers::got_peers(std::vector<tcp::endpoint> const& peers)
{
	if (m_data_callback) m_data_callback(peers);
}

get_peers::get_peers(
	node_impl& node
	, node_id target
	, data_callback const& dcallback
	, nodes_callback const& ncallback
	, bool noseeds)
	: find_data(node, target, ncallback)
	, m_data_callback(dcallback)
	, m_noseeds(noseeds)
{
}

char const* get_peers::name() const { return "get_peers"; }

bool get_peers::invoke(observer_ptr o)
{
	if (m_done)
	{
		m_invoke_count = -1;
		return false;
	}

	entry e;
	e["y"] = "q";
	entry& a = e["a"];

	e["q"] = "get_peers";
	a["info_hash"] = m_target.to_string();
	if (m_noseeds) a["noseed"] = 1;

	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

observer_ptr get_peers::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) get_peers_observer(this, ep, id));
#if TORRENT_USE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

obfuscated_get_peers::obfuscated_get_peers(
	node_impl& node
	, node_id info_hash
	, data_callback const& dcallback
	, nodes_callback const& ncallback
	, bool noseeds)
	: get_peers(node, info_hash, dcallback, ncallback, noseeds)
	, m_obfuscated(true)
{
}

char const* obfuscated_get_peers::name() const
{ return !m_obfuscated ? get_peers::name() : "get_peers [obfuscated]"; }

observer_ptr obfuscated_get_peers::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	if (m_obfuscated)
	{
		observer_ptr o(new (ptr) obfuscated_get_peers_observer(this, ep, id));
#if TORRENT_USE_ASSERTS
		o->m_in_constructor = false;
#endif
		return o;
	}
	else
	{
		observer_ptr o(new (ptr) get_peers_observer(this, ep, id));
#if TORRENT_USE_ASSERTS
		o->m_in_constructor = false;
#endif
		return o;
	}
}

bool obfuscated_get_peers::invoke(observer_ptr o)
{
	if (!m_obfuscated) return get_peers::invoke(o);

	node_id id = o->id();
	int shared_prefix = 160 - distance_exp(id, m_target);

	// when we get close to the target zone in the DHT
	// start using the correct info-hash, in order to
	// start receiving peers
	if (shared_prefix > m_node.m_table.depth() - 10)
	{
		m_obfuscated = false;
		// clear the queried bits on all successful nodes in
		// our node-list for this traversal algorithm, to
		// allow the get_peers traversal to regress in case
		// nodes further down end up being dead
		for (std::vector<observer_ptr>::iterator i = m_results.begin()
			, end(m_results.end()); i != end; ++i)
		{
			observer* o = i->get();
			// don't re-request from nodes that didn't respond
			if (o->flags & observer::flag_failed) continue;
			// don't interrupt with queries that are already in-flight
			if ((o->flags & observer::flag_alive) == 0) continue;
			o->flags &= ~(observer::flag_queried | observer::flag_alive);
		}
		return get_peers::invoke(o);
	}

	entry e;
	e["y"] = "q";
	e["q"] = "get_peers";
	entry& a = e["a"];

	// This logic will obfuscate the target info-hash
	// we're looking up, in order to preserve more privacy
	// on the DHT. This is done by only including enough
	// bits in the info-hash for the node we're querying to
	// give a good answer, but not more.

	// now, obfuscate the bits past shared_prefix + 3
	node_id mask = generate_prefix_mask(shared_prefix + 3);
	node_id obfuscated_target = generate_random_id() & ~mask;
	obfuscated_target |= m_target & mask;
	a["info_hash"] = obfuscated_target.to_string();

	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

void obfuscated_get_peers::done()
{
	if (!m_obfuscated) return get_peers::done();

	// oops, we failed to switch over to the non-obfuscated
	// mode early enough. do it now

	boost::intrusive_ptr<get_peers> ta(new get_peers(m_node, m_target
		, m_data_callback
		, m_nodes_callback
		, m_noseeds));

	// don't call these when the obfuscated_get_peers
	// is done, we're passing them on to be called when
	// ta completes.
	m_data_callback.clear();
	m_nodes_callback.clear();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << " [" << this << "]"
		<< " obfuscated get_peers phase 1 done, spawning get_peers [" << ta.get() << "]";
#endif

	int num_added = 0;
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end && num_added < 16; ++i)
	{
		observer_ptr o = *i;

		// only add nodes whose node ID we know and that
		// we know are alive
		if (o->flags & observer::flag_no_id) continue;
		if ((o->flags & observer::flag_alive) == 0) continue;

		ta->add_entry(o->id(), o->target_ep(), observer::flag_initial);
		++num_added;
	}

	ta->start();

	get_peers::done();
}

void obfuscated_get_peers_observer::reply(msg const& m)
{
	lazy_entry const* r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get()
			<< "] missing response dict";
#endif
		return;
	}

	lazy_entry const* id = r->dict_find_string("id");
	if (!id || id->string_length() != 20)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get()
			<< "] invalid id in response";
#endif
		return;
	}

	traversal_observer::reply(m);

	done();
}

} } // namespace libtorrent::dht
