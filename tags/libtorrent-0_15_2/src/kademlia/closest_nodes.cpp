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

#include <libtorrent/kademlia/closest_nodes.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>
#include "libtorrent/assert.hpp"

namespace libtorrent { namespace dht
{

closest_nodes_observer::~closest_nodes_observer()
{
	if (m_algorithm) m_algorithm->failed(m_self, true);
}

void closest_nodes_observer::reply(msg const& in)
{
	if (!m_algorithm)
	{
		TORRENT_ASSERT(false);
		return;
	}

	if (!in.nodes.empty())
	{
		for (msg::nodes_t::const_iterator i = in.nodes.begin()
			, end(in.nodes.end()); i != end; ++i)
		{
			m_algorithm->traverse(i->id, i->ep());
		}
	}
	m_algorithm->finished(m_self);
	m_algorithm = 0;
}

void closest_nodes_observer::timeout()
{
	if (!m_algorithm) return;
	m_algorithm->failed(m_self);
	m_algorithm = 0;
}

closest_nodes::closest_nodes(
	node_impl& node
	, node_id target
	, done_callback const& callback)
	: traversal_algorithm(node, target, node.m_table.begin(), node.m_table.end())
	, m_done_callback(callback)
{
	boost::intrusive_ptr<closest_nodes> self(this);
	add_requests();
}

void closest_nodes::invoke(node_id const& id, udp::endpoint addr)
{
	TORRENT_ASSERT(m_node.m_rpc.allocation_size() >= sizeof(closest_nodes_observer));
	void* ptr = m_node.m_rpc.allocator().malloc();
	if (ptr == 0)
	{
		done();
		return;
	}
	m_node.m_rpc.allocator().set_next_size(10);
	observer_ptr o(new (ptr) closest_nodes_observer(this, id));
#ifdef TORRENT_DEBUG
	o->m_in_constructor = false;
#endif
	m_node.m_rpc.invoke(messages::find_node, addr, o);
}

void closest_nodes::done()
{
	std::vector<node_entry> results;
	int num_results = m_node.m_table.bucket_size();
	for (std::vector<result>::iterator i = m_results.begin()
		, end(m_results.end()); i != end && num_results > 0; ++i)
	{
		if (i->flags & result::no_id) continue;
		if ((i->flags & result::queried) == 0) continue;
		results.push_back(node_entry(i->id, i->addr));
		--num_results;
	}
	m_done_callback(results);
}

} } // namespace libtorrent::dht

