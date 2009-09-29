/*

Copyright (c) 2006, Arvid Norberg & Daniel Wallin
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
	TORRENT_DECLARE_LOG(dht_tracker);
#endif

using detail::read_v4_endpoint;
using detail::read_v6_endpoint;
using detail::read_endpoint_list;

find_data_observer::~find_data_observer()
{
	if (m_algorithm) m_algorithm->failed(m_self);
}

void find_data_observer::reply(msg const& m)
{
	if (!m_algorithm)
	{
		TORRENT_ASSERT(false);
		return;
	}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	std::stringstream log_line;
	log_line << " incoming get_peer response [ ";
#endif

	lazy_entry const* r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << " missing response dict";
#endif
		return;
	}

	lazy_entry const* id = r->dict_find_string("id");
	if (!id || id->string_length() != 20)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << " invalid id in response";
#endif
		return;
	}

	lazy_entry const* token = r->dict_find_string("token");
	if (token)
	{
		m_algorithm->got_write_token(node_id(id->string_ptr()), token->string_value());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		log_line << " token: " << to_hex(token->string_value());
#endif
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
			log_line << " p: " << ((end - peers) / 6);
#endif
			while (end - peers >= 6)
				peer_list.push_back(read_v4_endpoint<tcp::endpoint>(peers));
		}
		else
		{
			// assume it's uTorrent/libtorrent format
			read_endpoint_list<tcp::endpoint>(n, peer_list);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " p: " << n->list_size();
#endif
		}
		m_algorithm->got_peers(peer_list);
	}

	// look for nodes
	n = r->dict_find_string("nodes");
	if (n)
	{
		std::vector<node_entry> node_list;
		char const* nodes = n->string_ptr();
		char const* end = nodes + n->string_length();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		log_line << " nodes: " << ((end - nodes) / 26);
#endif
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
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		log_line << " nodes2: " << n->list_size();
#endif
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
	m_algorithm->finished(m_self);
	m_algorithm = 0;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	log_line << " ]";
	TORRENT_LOG(dht_tracker) << log_line.str();
#endif
}

void find_data_observer::short_timeout()
{
	if (!m_algorithm) return;
	m_algorithm->failed(m_self, traversal_algorithm::short_timeout);
}

void find_data_observer::timeout()
{
	if (!m_algorithm) return;
	m_algorithm->failed(m_self);
	m_algorithm = 0;
}

find_data::find_data(
	node_impl& node
	, node_id target
	, data_callback const& dcallback
	, nodes_callback const& ncallback)
	: traversal_algorithm(node, target)
	, m_data_callback(dcallback)
	, m_nodes_callback(ncallback)
	, m_target(target)
	, m_done(false)
{
	for (routing_table::const_iterator i = node.m_table.begin()
		, end(node.m_table.end()); i != end; ++i)
	{
		add_entry(i->id, i->ep(), result::initial);
	}
}

void find_data::invoke(node_id const& id, udp::endpoint addr)
{
	if (m_done)
	{
		m_invoke_count = -1;
		return;
	}

	TORRENT_ASSERT(m_node.m_rpc.allocation_size() >= sizeof(find_data_observer));
	void* ptr = m_node.m_rpc.allocator().malloc();
	if (ptr == 0)
	{
		done();
		return;
	}
	m_node.m_rpc.allocator().set_next_size(10);
	observer_ptr o(new (ptr) find_data_observer(this, id));
#ifdef TORRENT_DEBUG
	o->m_in_constructor = false;
#endif
	entry e;
	e["y"] = "q";
	e["q"] = "get_peers";
	entry& a = e["a"];
	a["info_hash"] = id.to_string();
	m_node.m_rpc.invoke(e, addr, o);
}

void find_data::got_peers(std::vector<tcp::endpoint> const& peers)
{
	m_data_callback(peers);
}

void find_data::done()
{
	if (m_invoke_count != 0) return;

	std::vector<std::pair<node_entry, std::string> > results;
	int num_results = m_node.m_table.bucket_size();
	for (std::vector<result>::iterator i = m_results.begin()
		, end(m_results.end()); i != end && num_results > 0; ++i)
	{
		if (i->flags & result::no_id) continue;
		if ((i->flags & result::queried) == 0) continue;
		std::map<node_id, std::string>::iterator j = m_write_tokens.find(i->id);
		if (j == m_write_tokens.end()) continue;
		results.push_back(std::make_pair(node_entry(i->id, i->addr), j->second));
		--num_results;
	}
	m_nodes_callback(results);
}

} } // namespace libtorrent::dht

