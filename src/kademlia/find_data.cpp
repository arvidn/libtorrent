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

namespace libtorrent { namespace dht
{

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

	if (!m.write_token.empty())
		m_algorithm->got_write_token(m.id, m.write_token);

	if (!m.peers.empty())
		m_algorithm->got_data(&m);

	if (!m.nodes.empty())
	{
		for (msg::nodes_t::const_iterator i = m.nodes.begin()
			, end(m.nodes.end()); i != end; ++i)
		{
			m_algorithm->traverse(i->id, udp::endpoint(i->addr, i->port));
		}
	}
	m_algorithm->finished(m_self);
	m_algorithm = 0;
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
	: traversal_algorithm(node, target, node.m_table.begin(), node.m_table.end())
	, m_data_callback(dcallback)
	, m_nodes_callback(ncallback)
	, m_target(target)
	, m_done(false)
{
	boost::intrusive_ptr<find_data> self(this);
	add_requests();
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
	m_node.m_rpc.invoke(messages::get_peers, addr, o);
}

void find_data::got_data(msg const* m)
{
	m_data_callback(m->peers);
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

