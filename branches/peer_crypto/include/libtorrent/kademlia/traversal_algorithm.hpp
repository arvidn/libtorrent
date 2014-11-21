/*

Copyright (c) 2006-2014, Arvid Norberg & Daniel Wallin
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

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/address.hpp>

#include <boost/noncopyable.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/pool/pool.hpp>

namespace libtorrent { struct dht_lookup; }
namespace libtorrent { namespace dht
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(traversal);
#endif

class rpc_manager;
class node_impl;

// this class may not be instantiated as a stack object
struct traversal_algorithm : boost::noncopyable
{
	void traverse(node_id const& id, udp::endpoint addr);
	void finished(observer_ptr o);

	enum flags_t { prevent_request = 1, short_timeout = 2 };
	void failed(observer_ptr o, int flags = 0);
	virtual ~traversal_algorithm();
	void status(dht_lookup& l);
	void abort();

	void* allocate_observer();
	void free_observer(void* ptr);

	virtual char const* name() const;
	virtual void start();

	node_id const& target() const { return m_target; }

	void resort_results();
	void add_entry(node_id const& id, udp::endpoint addr, unsigned char flags);

	traversal_algorithm(node_impl& node, node_id target);
	int invoke_count() const { return m_invoke_count; }
	int branch_factor() const { return m_branch_factor; }

	node_impl& node() const { return m_node; }

protected:

	// returns true if we're done
	bool add_requests();

	void add_router_entries();
	void init();

	virtual void done();
	// should construct an algorithm dependent
	// observer in ptr.
	virtual observer_ptr new_observer(void* ptr
		, udp::endpoint const& ep, node_id const& id);

	virtual bool invoke(observer_ptr) { return false; }

	friend void intrusive_ptr_add_ref(traversal_algorithm* p)
	{
		TORRENT_ASSERT(p->m_ref_count < 0xffff);
		p->m_ref_count++;
	}

	friend void intrusive_ptr_release(traversal_algorithm* p)
	{
		if (--p->m_ref_count == 0)
			delete p;
	}

	node_impl& m_node;
	std::vector<observer_ptr> m_results;
	node_id const m_target;
	boost::uint16_t m_ref_count;
	boost::uint16_t m_invoke_count;
	boost::uint16_t m_branch_factor;
	boost::uint16_t m_responses;
	boost::uint16_t m_timeouts;

	// the IP addresses of the nodes in m_results
	std::set<boost::uint32_t> m_peer4_prefixes;
// no IPv6 support yet anyway
//	std::set<boost::uint64_t> m_peer6_prefixes;
};

struct traversal_observer : observer
{
	traversal_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: observer(algorithm, ep, id)
	{}

	// parses out "nodes" and keeps traversing
	virtual void reply(msg const&);
};

} } // namespace libtorrent::dht

#endif // TRAVERSAL_ALGORITHM_050324_HPP

