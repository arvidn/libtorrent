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

#ifndef FIND_DATA_050323_HPP
#define FIND_DATA_050323_HPP

#include <vector>
#include <map>

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <boost/optional.hpp>
#include <boost/function.hpp>

namespace libtorrent { namespace dht
{

typedef std::vector<char> packet_t;

class rpc_manager;
class node_impl;

// -------- find data -----------

class find_data : public traversal_algorithm
{
public:
	typedef boost::function<void(std::vector<tcp::endpoint> const&)> data_callback;
	typedef boost::function<void(std::vector<std::pair<node_entry, std::string> > const&)> nodes_callback;

	void got_data(msg const* m);
	void got_write_token(node_id const& n, std::string const& write_token)
	{ m_write_tokens[n] = write_token; }

	find_data(node_impl& node, node_id target
		, data_callback const& dcallback
		, nodes_callback const& ncallback);

	virtual char const* name() const { return "get_peers"; }
	node_id const target() const { return m_target; }

private:

	void done();
	void invoke(node_id const& id, udp::endpoint addr);

	data_callback m_data_callback;
	nodes_callback m_nodes_callback;
	std::map<node_id, std::string> m_write_tokens;
	node_id const m_target;
	bool m_done;
};

class find_data_observer : public observer
{
public:
	find_data_observer(
		boost::intrusive_ptr<find_data> const& algorithm
		, node_id self)
		: observer(algorithm->allocator())
		, m_algorithm(algorithm)
		, m_self(self)
	{}
	~find_data_observer();

	void send(msg& m)
	{
		m.reply = false;
		m.message_id = messages::get_peers;
		m.info_hash = m_algorithm->target();
	}

	void timeout();
	void reply(msg const&);
	void abort() { m_algorithm = 0; }

private:
	boost::intrusive_ptr<find_data> m_algorithm;
	node_id const m_self;
};

} } // namespace libtorrent::dht

#endif // FIND_DATA_050323_HPP

