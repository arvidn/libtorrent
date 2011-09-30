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

#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <libtorrent/io.hpp>

#include <boost/bind.hpp>

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(refresh)
#endif

refresh_observer::~refresh_observer()
{
	if (m_algorithm) m_algorithm->failed(m_self, true);
}

void refresh_observer::reply(msg const& in)
{
	if (!m_algorithm) return;

	if (!in.nodes.empty())
	{
		for (msg::nodes_t::const_iterator i = in.nodes.begin()
			, end(in.nodes.end()); i != end; ++i)
		{
			m_algorithm->traverse(i->id, udp::endpoint(i->addr, i->port));
		}
	}
	m_algorithm->finished(m_self);
	m_algorithm = 0;
}

void refresh_observer::timeout()
{
	if (!m_algorithm) return;
	m_algorithm->failed(m_self);
	m_algorithm = 0;
}

ping_observer::~ping_observer()
{
	if (m_algorithm) m_algorithm->ping_timeout(m_self, true);
}

void ping_observer::reply(msg const& m)
{
	if (!m_algorithm) return;
	
	m_algorithm->ping_reply(m_self);
	m_algorithm = 0;
}

void ping_observer::timeout()
{
	if (!m_algorithm) return;
	m_algorithm->ping_timeout(m_self);
	m_algorithm = 0;
}

void refresh::invoke(node_id const& nid, udp::endpoint addr)
{
	TORRENT_ASSERT(m_node.m_rpc.allocation_size() >= sizeof(refresh_observer));
	void* ptr = m_node.m_rpc.allocator().malloc();
	if (ptr == 0)
	{
		done();
		return;
	}
	m_node.m_rpc.allocator().set_next_size(10);
	observer_ptr o(new (ptr) refresh_observer( this, nid));
#ifdef TORRENT_DEBUG
	o->m_in_constructor = false;
#endif

	m_node.m_rpc.invoke(messages::find_node, addr, o);
}

void refresh::done()
{
	int max_results = m_node.m_table.bucket_size();
	m_leftover_nodes_iterator = (int)m_results.size() > max_results ?
		m_results.begin() + max_results : m_results.end();

	invoke_pings_or_finish();
}

void refresh::ping_reply(node_id nid)
{
	m_active_pings--;
	invoke_pings_or_finish();
}

void refresh::ping_timeout(node_id nid, bool prevent_request)
{
	m_active_pings--;
	invoke_pings_or_finish(prevent_request);
}

void refresh::invoke_pings_or_finish(bool prevent_request)
{
	if (prevent_request)
	{
		--m_max_active_pings;
		if (m_max_active_pings <= 0)
			m_max_active_pings = 1;
	}
	else
	{
		while (m_active_pings < m_max_active_pings)
		{
			if (m_leftover_nodes_iterator == m_results.end()) break;

			result const& node = *m_leftover_nodes_iterator;

			// Skip initial nodes
			if (node.flags & result::initial)
			{
				++m_leftover_nodes_iterator;
				continue;
			}

#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
				TORRENT_ASSERT(m_node.m_rpc.allocation_size() >= sizeof(ping_observer));
				void* ptr = m_node.m_rpc.allocator().malloc();
				if (ptr == 0) return;
				m_node.m_rpc.allocator().set_next_size(10);
				observer_ptr o(new (ptr) ping_observer(this, node.id));
#ifdef TORRENT_DEBUG
				o->m_in_constructor = false;
#endif
				m_node.m_rpc.invoke(messages::ping, node.addr, o);
				++m_active_pings;
				++m_leftover_nodes_iterator;
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e) {}
#endif
		}
	}

	if (m_active_pings == 0)
	{
		m_done_callback();
	}
}

} } // namespace libtorrent::dht

