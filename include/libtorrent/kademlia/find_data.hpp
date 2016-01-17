/*

Copyright (c) 2006-2016, Arvid Norberg & Daniel Wallin
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

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <map>

#include <boost/optional.hpp>
#include <boost/function/function1.hpp>
#include <boost/function/function2.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent { namespace dht
{

typedef std::vector<char> packet_t;

class rpc_manager;
class node;

// -------- find data -----------

struct find_data : traversal_algorithm
{
	typedef boost::function<void(std::vector<std::pair<node_entry, std::string> > const&)> nodes_callback;

	find_data(node & node, node_id target
		, nodes_callback const& ncallback);

	void got_write_token(node_id const& n, std::string const& write_token);

	virtual void start();

	virtual char const* name() const;

	node_id const target() const { return m_target; }

protected:

	virtual void done();
	virtual observer_ptr new_observer(void* ptr, udp::endpoint const& ep
		, node_id const& id);

	nodes_callback m_nodes_callback;
	std::map<node_id, std::string> m_write_tokens;
	bool m_done;
};

struct find_data_observer : traversal_observer
{
	find_data_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: traversal_observer(algorithm, ep, id)
	{}

	virtual void reply(msg const&);
};

} } // namespace libtorrent::dht

#endif // FIND_DATA_050323_HPP

