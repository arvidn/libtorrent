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

#ifndef CLOSEST_NODES_050323_HPP
#define CLOSEST_NODES_050323_HPP

#include <vector>

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <boost/function.hpp>

namespace libtorrent { namespace dht
{

class rpc_manager;

// -------- closest nodes -----------

class closest_nodes : public traversal_algorithm
{
public:
	typedef boost::function<
		void(std::vector<node_entry> const&)
	> done_callback;

	closest_nodes(
		node_impl& node
		, node_id target
		, done_callback const& callback
	);

	virtual char const* name() const { return "closest nodes"; }

private:
	void done();
	void invoke(node_id const& id, udp::endpoint addr);
	
	done_callback m_done_callback;
};

class closest_nodes_observer : public observer
{
public:
	closest_nodes_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, node_id self)
		: observer(algorithm->allocator())
		, m_algorithm(algorithm)
		, m_self(self)
	{}
	~closest_nodes_observer();

	void send(msg& p)
	{
		p.info_hash = m_algorithm->target();
	}

	void timeout();
	void reply(msg const&);
	void abort() { m_algorithm = 0; }

private:
	boost::intrusive_ptr<traversal_algorithm> m_algorithm;
	node_id const m_self;
};

} } // namespace libtorrent::dht

#endif // CLOSEST_NODES_050323_HPP

