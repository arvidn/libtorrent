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

#ifndef TRAVERSAL_ALGORITHM_050324_HPP
#define TRAVERSAL_ALGORITHM_050324_HPP

#include <vector>
#include <set>
#include <memory>

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/address.hpp>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/noncopyable.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent { struct dht_lookup; }
namespace libtorrent { namespace dht
{

class node;

// this class may not be instantiated as a stack object
struct TORRENT_EXTRA_EXPORT traversal_algorithm : boost::noncopyable
	, std::enable_shared_from_this<traversal_algorithm>
{
	void traverse(node_id const& id, udp::endpoint const& addr);
	void finished(observer_ptr o);

	enum flags_t { prevent_request = 1, short_timeout = 2 };
	void failed(observer_ptr o, int flags = 0);
	virtual ~traversal_algorithm();
	void status(dht_lookup& l);

	virtual char const* name() const;
	virtual void start();

	node_id const& target() const { return m_target; }

	void resort_results();
	void add_entry(node_id const& id, udp::endpoint const& addr, unsigned char flags);

	traversal_algorithm(node& dht_node, node_id const& target);
	int invoke_count() const { TORRENT_ASSERT(m_invoke_count >= 0); return m_invoke_count; }
	int branch_factor() const { TORRENT_ASSERT(m_branch_factor >= 0); return m_branch_factor; }

	node& get_node() const { return m_node; }

protected:

	std::shared_ptr<traversal_algorithm> self()
	{ return shared_from_this(); }

	// returns true if we're done
	bool add_requests();

	void add_router_entries();
	void init();

	virtual void done();
	// should construct an algorithm dependent
	// observer in ptr.
	virtual observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id);

	virtual bool invoke(observer_ptr) { return false; }

	int num_responses() const { return m_responses; }
	int num_timeouts() const { return m_timeouts; }

	node& m_node;
	std::vector<observer_ptr> m_results;

private:

	node_id const m_target;
	std::int16_t m_invoke_count = 0;
	std::int16_t m_branch_factor = 3;
	std::int16_t m_responses = 0;
	std::int16_t m_timeouts = 0;

	// the IP addresses of the nodes in m_results
	std::set<std::uint32_t> m_peer4_prefixes;
#if TORRENT_USE_IPV6
	std::set<std::uint64_t> m_peer6_prefixes;
#endif
};

struct traversal_observer : observer
{
	traversal_observer(
		std::shared_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: observer(algorithm, ep, id)
	{}

	// parses out "nodes" and keeps traversing
	virtual void reply(msg const&);
};

} } // namespace libtorrent::dht

#endif // TRAVERSAL_ALGORITHM_050324_HPP
