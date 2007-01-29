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

#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/logging.hpp>

#include <libtorrent/io.hpp>

#include <boost/bind.hpp>

using boost::bind;

namespace libtorrent { namespace dht
{

using asio::ip::udp;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(refresh)
#endif

typedef boost::shared_ptr<observer> observer_ptr;

class refresh_observer : public observer
{
public:
	refresh_observer(
		boost::intrusive_ptr<refresh> const& algorithm
		, node_id self
		, node_id target
	)
		: m_target(target) 
		, m_self(self)
		, m_algorithm(algorithm)
	{}
	~refresh_observer();

	void send(msg& m)
	{
		m.info_hash = m_target;
	}

	void timeout();
	void reply(msg const& m);
	void abort() { m_algorithm = 0; }


private:
	node_id const m_target;
	node_id const m_self;
	boost::intrusive_ptr<refresh> m_algorithm;
};

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
			m_algorithm->traverse(i->id, i->addr);
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

class ping_observer : public observer
{
public:
	ping_observer(
		boost::intrusive_ptr<refresh> const& algorithm
		, node_id self
	)
		: m_self(self)
		, m_algorithm(algorithm)
	{}
	~ping_observer();

	void send(msg& p) {}
	void timeout();
	void reply(msg const& m);
	void abort() { m_algorithm = 0; }


private:
	node_id const m_self;
	boost::intrusive_ptr<refresh> m_algorithm;
};

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
	observer_ptr p(new refresh_observer(
		this
		, nid
		, m_target
	));

	m_rpc.invoke(messages::find_node, addr, p);
}

void refresh::done()
{
	m_leftover_nodes_iterator = (int)m_results.size() > m_max_results ?
		m_results.begin() + m_max_results : m_results.end();

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

			try
			{
				observer_ptr p(new ping_observer(this, node.id));
				m_rpc.invoke(messages::ping, node.addr, p);
				++m_active_pings;
				++m_leftover_nodes_iterator;
			}
			catch (std::exception& e) {}
		}
	}

	if (m_active_pings == 0)
	{
		m_done_callback();
	}
}

} } // namespace libtorrent::dht

