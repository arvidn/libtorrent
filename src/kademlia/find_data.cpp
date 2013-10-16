/*

Copyright (c) 2006-2012, Arvid Norberg & Daniel Wallin
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

#include "libtorrent/pch.hpp"

#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/io.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/socket_io.hpp>
#include <vector>

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(traversal);
#endif

using detail::read_endpoint_list;
using detail::read_v4_endpoint;
#if TORRENT_USE_IPV6
using detail::read_v6_endpoint;
#endif

void find_data_observer::reply(msg const& m)
{
	lazy_entry const* r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get() << "] missing response dict";
#endif
		return;
	}

	lazy_entry const* id = r->dict_find_string("id");
	if (!id || id->string_length() != 20)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get() << "] invalid id in response";
#endif
		return;
	}
	lazy_entry const* token = r->dict_find_string("token");
	if (token)
	{
		static_cast<find_data*>(m_algorithm.get())->got_write_token(
			node_id(id->string_ptr()), token->string_value());
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
			TORRENT_LOG(traversal)
				<< "[" << m_algorithm.get() << "] PEERS"
				<< " invoke-count: " << m_algorithm->invoke_count()
				<< " branch-factor: " << m_algorithm->branch_factor()
				<< " addr: " << m.addr
				<< " id: " << node_id(id->string_ptr())
				<< " distance: " << distance_exp(m_algorithm->target(), node_id(id->string_ptr()))
				<< " p: " << ((end - peers) / 6);
#endif
			while (end - peers >= 6)
				peer_list.push_back(read_v4_endpoint<tcp::endpoint>(peers));
		}
		else
		{
			// assume it's uTorrent/libtorrent format
			read_endpoint_list<tcp::endpoint>(n, peer_list);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(traversal)
				<< "[" << m_algorithm.get() << "] PEERS"
				<< " invoke-count: " << m_algorithm->invoke_count()
				<< " branch-factor: " << m_algorithm->branch_factor()
				<< " addr: " << m.addr
				<< " id: " << node_id(id->string_ptr())
				<< " distance: " << distance_exp(m_algorithm->target(), node_id(id->string_ptr()))
				<< " p: " << n->list_size();
#endif
		}
		static_cast<find_data*>(m_algorithm.get())->got_peers(peer_list);
	}

	// look for nodes
	n = r->dict_find_string("nodes");
	if (n)
	{
		std::vector<node_entry> node_list;
		char const* nodes = n->string_ptr();
		char const* end = nodes + n->string_length();

		while (end - nodes >= 26)
		{
			node_id id;
			std::copy(nodes, nodes + 20, id.begin());
			nodes += 20;
			m_algorithm->traverse(id, read_v4_endpoint<udp::endpoint>(nodes));
		}
	}

	n = r->dict_find_list("nodes2");
	if (n)
	{
		for (int i = 0; i < n->list_size(); ++i)
		{
			lazy_entry const* p = n->list_at(0);
			if (p->type() != lazy_entry::string_t) continue;
			if (p->string_length() < 6 + 20) continue;
			char const* in = p->string_ptr();

			node_id id;
			std::copy(in, in + 20, id.begin());
			in += 20;
			if (p->string_length() == 6 + 20)
				m_algorithm->traverse(id, read_v4_endpoint<udp::endpoint>(in));
#if TORRENT_USE_IPV6
			else if (p->string_length() == 18 + 20)
				m_algorithm->traverse(id, read_v6_endpoint<udp::endpoint>(in));
#endif
		}
	}
	done();
}

void add_entry_fun(void* userdata, node_entry const& e)
{
	traversal_algorithm* f = (traversal_algorithm*)userdata;
	f->add_entry(e.id, e.ep(), observer::flag_initial);
}

find_data::find_data(
	node_impl& node
	, node_id target
	, data_callback const& dcallback
	, nodes_callback const& ncallback
	, bool noseeds)
	: traversal_algorithm(node, target)
	, m_data_callback(dcallback)
	, m_nodes_callback(ncallback)
	, m_target(target)
	, m_done(false)
	, m_got_peers(false)
	, m_noseeds(noseeds)
{
}

void find_data::start()
{
	// if the user didn't add seed-nodes manually, grab a bunch of nodes from the
	// routing table
	if (m_results.empty())
		m_node.m_table.for_each_node(&add_entry_fun, 0, (traversal_algorithm*)this);

	traversal_algorithm::start();
}

observer_ptr find_data::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) find_data_observer(this, ep, id));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

char const* find_data::name() const { return "get_peers"; }

bool find_data::invoke(observer_ptr o)
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

void find_data::got_peers(std::vector<tcp::endpoint> const& peers)
{
	if (!peers.empty()) m_got_peers = true;
	if (m_data_callback) m_data_callback(peers);
}

void find_data::done()
{
	if (m_invoke_count != 0) return;

	m_done = true;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << "[" << this << "] get_peers DONE";
#endif

	std::vector<std::pair<node_entry, std::string> > results;
	int num_results = m_node.m_table.bucket_size();
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end && num_results > 0; ++i)
	{
		observer_ptr const& o = *i;
		if (o->flags & observer::flag_no_id) continue;
		if ((o->flags & observer::flag_queried) == 0) continue;
		std::map<node_id, std::string>::iterator j = m_write_tokens.find(o->id());
		if (j == m_write_tokens.end()) continue;
		results.push_back(std::make_pair(node_entry(o->id(), o->target_ep()), j->second));
		--num_results;
	}
	if (m_nodes_callback) m_nodes_callback(results, m_got_peers);

	traversal_algorithm::done();
}

obfuscated_get_peers::obfuscated_get_peers(
	node_impl& node
	, node_id info_hash
	, data_callback const& dcallback
	, nodes_callback const& ncallback
	, bool noseeds)
	: find_data(node, info_hash, dcallback, ncallback, noseeds)
{
}

char const* obfuscated_get_peers::name() const { return "get_peers [obfuscated]"; }

observer_ptr obfuscated_get_peers::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) find_data_observer(this, ep, id));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

bool obfuscated_get_peers::invoke(observer_ptr o)
{
	entry e;
	e["y"] = "q";
	e["q"] = "find_node";
	entry& a = e["a"];

	// This logic will obfuscate the target info-hash
	// we're looking up, in order to preserve more privacy
	// on the DHT. This is done by only including enough
	// bits in the info-hash for the node we're querying to
	// give a good answer, but not more.

	node_id id = o->id();
	int shared_prefix = 160 - distance_exp(id, m_target);

	// now, obfuscate the bits past shared_prefix + 5
	node_id obfuscated_target = generate_random_id();
	obfuscated_target >>= shared_prefix + 3;
	obfuscated_target^= m_target;
	a["target"] = obfuscated_target.to_string();

	return m_node.m_rpc.invoke(e, o->target_ep(), o);
}

void obfuscated_get_peers::done()
{
	boost::intrusive_ptr<find_data> ta(new find_data(m_node, m_target
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
		, end(m_results.end()); i != end && num_added < 10; ++i)
	{
		observer_ptr o = *i;

		// only add nodes whose node ID we knoe and that
		// we know are alive
		if (o->flags & observer::flag_no_id) continue;
		if ((o->flags & observer::flag_alive) == 0) continue;

		ta->add_entry(o->id(), o->target_ep(), observer::flag_initial);
		++num_added;
	}

	ta->start();

	find_data::done();
}

} } // namespace libtorrent::dht

