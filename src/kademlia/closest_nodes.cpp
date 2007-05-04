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

#include <libtorrent/kademlia/closest_nodes.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>

namespace libtorrent { namespace dht
{

using asio::ip::udp;

typedef boost::shared_ptr<observer> observer_ptr;

class closest_nodes_observer : public observer
{
public:
	closest_nodes_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, node_id self
		, node_id target)
		: m_algorithm(algorithm)
		, m_target(target) 
		, m_self(self)
	{}
	~closest_nodes_observer();

	void send(msg& p)
	{
		p.info_hash = m_target;
	}

	void timeout();
	void reply(msg const&);
	void abort() { m_algorithm = 0; }

private:
	boost::intrusive_ptr<traversal_algorithm> m_algorithm;
	node_id const m_target;
	node_id const m_self;
};

closest_nodes_observer::~closest_nodes_observer()
{
	if (m_algorithm) m_algorithm->failed(m_self, true);
}

void closest_nodes_observer::reply(msg const& in)
{
	if (!m_algorithm)
	{
		assert(false);
		return;
	}

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

void closest_nodes_observer::timeout()
{
	if (!m_algorithm) return;
	m_algorithm->failed(m_self);
	m_algorithm = 0;
}


closest_nodes::closest_nodes(
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
{
	boost::intrusive_ptr<closest_nodes> self(this);
	add_requests();
}

void closest_nodes::invoke(node_id const& id, udp::endpoint addr)
{
	observer_ptr p(new closest_nodes_observer(this, id, m_target));
	m_rpc.invoke(messages::find_node, addr, p);
}

void closest_nodes::done()
{
	std::vector<node_entry> results;
	int result_size = m_table.bucket_size();
	if (result_size > (int)m_results.size()) result_size = (int)m_results.size();
	for (std::vector<result>::iterator i = m_results.begin()
		, end(m_results.begin() + result_size); i != end; ++i)
	{
		results.push_back(node_entry(i->id, i->addr));
	}
	m_done_callback(results);
}

void closest_nodes::initiate(
	node_id target
	, int branch_factor
	, int max_results
	, routing_table& table
	, rpc_manager& rpc
	, done_callback const& callback
)
{
	new closest_nodes(target, branch_factor, max_results, table, rpc, callback);
}

} } // namespace libtorrent::dht

