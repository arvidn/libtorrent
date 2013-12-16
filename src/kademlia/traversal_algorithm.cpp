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

#include "libtorrent/time.hpp" // for total_seconds

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/session_status.hpp>
#include "libtorrent/broadcast_socket.hpp" // for cidr_distance

#include <boost/bind.hpp>

namespace libtorrent { namespace dht
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(traversal)
#endif

observer_ptr traversal_algorithm::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) null_observer(boost::intrusive_ptr<traversal_algorithm>(this), ep, id));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

traversal_algorithm::traversal_algorithm(
	node_impl& node
	, node_id target)
	: m_ref_count(0)
	, m_node(node)
	, m_target(target)
	, m_invoke_count(0)
	, m_branch_factor(3)
	, m_responses(0)
	, m_timeouts(0)
	, m_num_target_nodes(m_node.m_table.bucket_size() * 2)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << " [" << this << "] new traversal process. Target: " << target;
#endif
}

// returns true of lhs and rhs are too close to each other to appear
// in the same DHT search under different node IDs
bool compare_ip_cidr(observer_ptr const& lhs, observer_ptr const& rhs)
{
	if (lhs->target_addr().is_v4() != rhs->target_addr().is_v4())
		return false;
	// the number of bits in the IPs that may match. If
	// more bits that this matches, something suspicious is
	// going on and we shouldn't add the second one to our
	// routing table
	int cutoff = rhs->target_addr().is_v4() ? 4 : 64;
	int dist = cidr_distance(lhs->target_addr(), rhs->target_addr());
	return dist <= cutoff;
}

void traversal_algorithm::add_entry(node_id const& id, udp::endpoint addr, unsigned char flags)
{
	TORRENT_ASSERT(m_node.m_rpc.allocation_size() >= sizeof(find_data_observer));
	void* ptr = m_node.m_rpc.allocate_observer();
	if (ptr == 0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << this << ":" << name()
			<< "] failed to allocate memory for observer. aborting!";
#endif
		done();
		return;
	}
	observer_ptr o = new_observer(ptr, addr, id);
	if (id.is_all_zeros())
	{
		o->set_id(generate_random_id());
		o->flags |= observer::flag_no_id;
	}

	o->flags |= flags;

	std::vector<observer_ptr>::iterator i = std::lower_bound(
		m_results.begin()
		, m_results.end()
		, o
		, boost::bind(
			compare_ref
			, boost::bind(&observer::id, _1)
			, boost::bind(&observer::id, _2)
			, m_target
		)
	);

	if (i == m_results.end() || (*i)->id() != id)
	{
		if (m_node.settings().restrict_search_ips
			&& !(flags & observer::flag_initial))
		{
			// don't allow multiple entries from IPs very close to each other
			std::vector<observer_ptr>::iterator j = std::find_if(
				m_results.begin(), m_results.end(), boost::bind(&compare_ip_cidr, _1, o));

			if (j != m_results.end())
			{
				// we already have a node in this search with an IP very
				// close to this one. We know that it's not the same, because
				// it claims a different node-ID. Ignore this to avoid attacks
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(traversal) << "ignoring DHT search entry: " << o->id()
				<< " " << o->target_addr()
				<< " existing node: "
				<< (*j)->id() << " " << (*j)->target_addr();
#endif
				return;
			}
		}
		TORRENT_ASSERT(std::find_if(m_results.begin(), m_results.end()
			, boost::bind(&observer::id, _1) == id) == m_results.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << this << ":" << name()
			<< "] adding result: " << id << " " << addr;
#endif
		i = m_results.insert(i, o);
	}

	if (m_results.size() > 100)
	{
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		for (int i = 100; i < m_results.size(); ++i)
			m_results[i]->m_was_abandoned = true;
#endif
		m_results.resize(100);
	}
}

void traversal_algorithm::start()
{
	// in case the routing table is empty, use the
	// router nodes in the table
	if (m_results.empty()) add_router_entries();
	init();
	add_requests();
	if (m_invoke_count == 0) done();
}

void* traversal_algorithm::allocate_observer()
{
	return m_node.m_rpc.allocate_observer();
}

void traversal_algorithm::free_observer(void* ptr)
{
	m_node.m_rpc.free_observer(ptr);
}

void traversal_algorithm::traverse(node_id const& id, udp::endpoint addr)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	if (id.is_all_zeros())
		TORRENT_LOG(traversal) << time_now_string() << "[" << this << ":" << name()
			<< "] WARNING: node returned a list which included a node with id 0";
#endif
	add_entry(id, addr, 0);
}

void traversal_algorithm::finished(observer_ptr o)
{
#ifdef TORRENT_DEBUG
	std::vector<observer_ptr>::iterator i = std::find(
		m_results.begin(), m_results.end(), o);

	TORRENT_ASSERT(i != m_results.end() || m_results.size() == 100);
#endif

	// if this flag is set, it means we increased the
	// branch factor for it, and we should restore it
	if (o->flags & observer::flag_short_timeout)
		--m_branch_factor;

	TORRENT_ASSERT(o->flags & observer::flag_queried);
	o->flags |= observer::flag_alive;

	++m_responses;
	--m_invoke_count;
	TORRENT_ASSERT(m_invoke_count >= 0);
	add_requests();
	if (m_invoke_count == 0) done();
}

// prevent request means that the total number of requests has
// overflown. This query failed because it was the oldest one.
// So, if this is true, don't make another request
void traversal_algorithm::failed(observer_ptr o, int flags)
{
	TORRENT_ASSERT(m_invoke_count >= 0);

	if (m_results.empty()) return;

	TORRENT_ASSERT(o->flags & observer::flag_queried);
	if (flags & short_timeout)
	{
		// short timeout means that it has been more than
		// two seconds since we sent the request, and that
		// we'll most likely not get a response. But, in case
		// we do get a late response, keep the handler
		// around for some more, but open up the slot
		// by increasing the branch factor
		if ((o->flags & observer::flag_short_timeout) == 0)
			++m_branch_factor;
		o->flags |= observer::flag_short_timeout;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << " [" << this << ":" << name()
			<< "] first chance timeout: "
			<< o->id() << " " << o->target_ep()
			<< " branch-factor: " << m_branch_factor
			<< " invoke-count: " << m_invoke_count;
#endif
	}
	else
	{
		o->flags |= observer::flag_failed;
		// if this flag is set, it means we increased the
		// branch factor for it, and we should restore it
		if (o->flags & observer::flag_short_timeout)
			--m_branch_factor;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << " [" << this << ":" << name()
			<< "] failed: " << o->id() << " " << o->target_ep()
			<< " branch-factor: " << m_branch_factor
			<< " invoke-count: " << m_invoke_count;
#endif
		// don't tell the routing table about
		// node ids that we just generated ourself
		if ((o->flags & observer::flag_no_id) == 0)
			m_node.m_table.node_failed(o->id(), o->target_ep());
		++m_timeouts;
		--m_invoke_count;
		TORRENT_ASSERT(m_invoke_count >= 0);
	}

	if (flags & prevent_request)
	{
		--m_branch_factor;
		if (m_branch_factor <= 0) m_branch_factor = 1;
	}
	add_requests();
	if (m_invoke_count == 0) done();
}

void traversal_algorithm::done()
{
	// delete all our references to the observer objects so
	// they will in turn release the traversal algorithm
	m_results.clear();
}

void traversal_algorithm::add_requests()
{
	int results_target = m_num_target_nodes;

	// Find the first node that hasn't already been queried.
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end
		&& results_target > 0 && m_invoke_count < m_branch_factor; ++i)
	{
		if ((*i)->flags & observer::flag_alive) --results_target;
		if ((*i)->flags & observer::flag_queried) continue;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << " [" << this << ":" << name() << "]"
			<< " nodes-left: " << (m_results.end() - i)
			<< " invoke-count: " << m_invoke_count
			<< " branch-factor: " << m_branch_factor;
#endif

		(*i)->flags |= observer::flag_queried;
		if (invoke(*i))
		{
			TORRENT_ASSERT(m_invoke_count >= 0);
			++m_invoke_count;
		}
		else
		{
			(*i)->flags |= observer::flag_failed;
		}
	}
}

void traversal_algorithm::add_router_entries()
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << " using router nodes to initiate traversal algorithm. "
		<< std::distance(m_node.m_table.router_begin(), m_node.m_table.router_end()) << " routers";
#endif
	for (routing_table::router_iterator i = m_node.m_table.router_begin()
		, end(m_node.m_table.router_end()); i != end; ++i)
	{
		add_entry(node_id(0), *i, observer::flag_initial);
	}
}

void traversal_algorithm::init()
{
	// update the last activity of this bucket
	m_node.m_table.touch_bucket(m_target);
	m_branch_factor = m_node.branch_factor();
	m_node.add_traversal_algorithm(this);
}

traversal_algorithm::~traversal_algorithm()
{
	m_node.remove_traversal_algorithm(this);
}

void traversal_algorithm::status(dht_lookup& l)
{
	l.timeouts = m_timeouts;
	l.responses = m_responses;
	l.outstanding_requests = m_invoke_count;
	l.branch_factor = m_branch_factor;
	l.type = name();
	l.nodes_left = 0;
	l.first_timeout = 0;

	int last_sent = INT_MAX;
	ptime now = time_now();
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end; ++i)
	{
		observer& o = **i;
		if (o.flags & observer::flag_queried)
		{
			last_sent = (std::min)(last_sent, int(total_seconds(now - o.sent())));
			if (o.has_short_timeout()) ++l.first_timeout;
			continue;
		}
		++l.nodes_left;
	}
	l.last_sent = last_sent;
}

} } // namespace libtorrent::dht

