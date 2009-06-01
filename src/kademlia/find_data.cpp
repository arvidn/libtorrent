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

	if (!m.peers.empty())
	{
		m_algorithm->got_data(&m);
	}
	else
	{
		for (msg::nodes_t::const_iterator i = m.nodes.begin()
			, end(m.nodes.end()); i != end; ++i)
		{
			m_algorithm->traverse(i->id, i->addr);	
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
	node_id target
	, int branch_factor
	, int max_results
	, routing_table& table
	, rpc_manager& rpc
	, done_callback const& callback
)
	: traversal_algorithm(
		target
		, branch_factor
		, max_results
		, table
		, rpc
		, table.begin()
		, table.end()
	)
	, m_done_callback(callback)
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

	TORRENT_ASSERT(m_rpc.allocation_size() >= sizeof(find_data_observer));
	observer_ptr o(new (m_rpc.allocator().malloc()) find_data_observer(this, id, m_target));
#ifdef TORRENT_DEBUG
	o->m_in_constructor = false;
#endif
	m_rpc.invoke(messages::get_peers, addr, o);
}

void find_data::got_data(msg const* m)
{
	m_done = true;
	m_done_callback(m);
}

void find_data::done()
{
	if (m_invoke_count != 0) return;
	if (!m_done) m_done_callback(0);
}

void find_data::initiate(
	node_id target
	, int branch_factor
	, int max_results
	, routing_table& table
	, rpc_manager& rpc
	, done_callback const& callback
)
{
	new find_data(target, branch_factor, max_results, table, rpc, callback);
}

} } // namespace libtorrent::dht

