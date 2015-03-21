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

#include "libtorrent/time.hpp" // for total_seconds

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/session_status.hpp>
#include "libtorrent/broadcast_socket.hpp" // for cidr_distance
#include <libtorrent/socket_io.hpp> // for read_*_endpoint

#include <boost/bind.hpp>

namespace libtorrent { namespace dht
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(traversal)
#endif

using detail::read_v4_endpoint;
#if TORRENT_USE_IPV6
using detail::read_v6_endpoint;
#endif

#if TORRENT_USE_ASSERTS
template <class It, class Cmp>
bool is_sorted(It b, It e, Cmp cmp)
{
	if (b == e) return true;

	typename std::iterator_traits<It>::value_type v = *b;
	++b;
	while (b != e)
	{
		if (cmp(*b, v)) return false;
		v = *b;
		++b;
	}
	return true;
}
#endif

observer_ptr traversal_algorithm::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) null_observer(boost::intrusive_ptr<traversal_algorithm>(this), ep, id));
#if TORRENT_USE_ASSERTS
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
	, m_num_target_nodes(m_node.m_table.bucket_size())
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << "[" << this << "] NEW"
		" target: " << target
		<< " k: " << m_node.m_table.bucket_size()
		;
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

void traversal_algorithm::resort_results()
{
	std::sort(
		m_results.begin()
		, m_results.end()
		, boost::bind(
			compare_ref
			, boost::bind(&observer::id, _1)
			, boost::bind(&observer::id, _2)
			, m_target
		)
	);
}

void traversal_algorithm::add_entry(node_id const& id, udp::endpoint addr, unsigned char flags)
{
	TORRENT_ASSERT(m_node.m_rpc.allocation_size() >= sizeof(find_data_observer));
	void* ptr = m_node.m_rpc.allocate_observer();
	if (ptr == 0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << this << "] failed to allocate memory for observer. aborting!";
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

	TORRENT_ASSERT(libtorrent::dht::is_sorted(m_results.begin(), m_results.end()
		, boost::bind(
			compare_ref
			, boost::bind(&observer::id, _1)
			, boost::bind(&observer::id, _2)
			, m_target)
		));

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
			TORRENT_LOG(traversal) << "[" << this << "] IGNORING result "
				<< "id: " << o->id()
				<< " address: " << o->target_addr()
				<< " existing node: "
				<< (*j)->id() << " " << (*j)->target_addr()
				<< " distance: " << distance_exp(m_target, o->id())
				<< " type: " << name()
				;
#endif
				return;
			}
		}

		TORRENT_ASSERT((o->flags & observer::flag_no_id) || std::find_if(m_results.begin(), m_results.end()
			, boost::bind(&observer::id, _1) == id) == m_results.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << this << "] ADD id: " << id
			<< " address: " << addr
			<< " distance: " << distance_exp(m_target, id)
			<< " invoke-count: " << m_invoke_count
			<< " type: " << name()
			;
#endif
		i = m_results.insert(i, o);

		TORRENT_ASSERT(libtorrent::dht::is_sorted(m_results.begin(), m_results.end()
			, boost::bind(
				compare_ref
				, boost::bind(&observer::id, _1)
				, boost::bind(&observer::id, _2)
				, m_target)
			));
	}

	if (m_results.size() > 100)
	{
#if TORRENT_USE_ASSERTS
		for (int i = 100; i < int(m_results.size()); ++i)
			m_results[i]->m_was_abandoned = true;
#endif
		m_results.resize(100);
	}
}

void traversal_algorithm::start()
{
	// in case the routing table is empty, use the
	// router nodes in the table
	if (m_results.size() < 3) add_router_entries();
	init();
	bool is_done = add_requests();
	if (is_done) done();
}

void* traversal_algorithm::allocate_observer()
{
	return m_node.m_rpc.allocate_observer();
}

void traversal_algorithm::free_observer(void* ptr)
{
	m_node.m_rpc.free_observer(ptr);
}

char const* traversal_algorithm::name() const
{
	return "traversal_algorithm";
}

void traversal_algorithm::traverse(node_id const& id, udp::endpoint addr)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	if (id.is_all_zeros())
	{
		TORRENT_LOG(traversal) << time_now_string() << "[" << this << "] WARNING node returned a list which included a node with id 0";
	}
#endif

	// let the routing table know this node may exist
	m_node.m_table.heard_about(id, addr);

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
	{
		TORRENT_ASSERT(m_branch_factor > 0);
		--m_branch_factor;
	}

	TORRENT_ASSERT(o->flags & observer::flag_queried);
	o->flags |= observer::flag_alive;

	++m_responses;
	--m_invoke_count;
	TORRENT_ASSERT(m_invoke_count >= 0);
	bool is_done = add_requests();
	if (is_done) done();
}

// prevent request means that the total number of requests has
// overflown. This query failed because it was the oldest one.
// So, if this is true, don't make another request
void traversal_algorithm::failed(observer_ptr o, int flags)
{
	TORRENT_ASSERT(m_invoke_count >= 0);

	// don't tell the routing table about
	// node ids that we just generated ourself
	if ((o->flags & observer::flag_no_id) == 0)
		m_node.m_table.node_failed(o->id(), o->target_ep());

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
		TORRENT_LOG(traversal) << "[" << this << "] 1ST_TIMEOUT "
			<< " id: " << o->id()
			<< " distance: " << distance_exp(m_target, o->id())
			<< " addr: " << o->target_ep()
			<< " branch-factor: " << m_branch_factor
			<< " invoke-count: " << m_invoke_count
			<< " type: " << name()
			;
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
		TORRENT_LOG(traversal) << "[" << this << "] TIMEOUT "
			<< " id: " << o->id()
			<< " distance: " << distance_exp(m_target, o->id())
			<< " addr: " << o->target_ep()
			<< " branch-factor: " << m_branch_factor
			<< " invoke-count: " << m_invoke_count
			<< " type: " << name()
			;
#endif

		++m_timeouts;
		--m_invoke_count;
		TORRENT_ASSERT(m_invoke_count >= 0);
	}

	if (flags & prevent_request)
	{
		--m_branch_factor;
		if (m_branch_factor <= 0) m_branch_factor = 1;
	}
	bool is_done = add_requests();
	if (is_done) done();
}

void traversal_algorithm::done()
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	int results_target = m_num_target_nodes;
	int closest_target = 160;

	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end && results_target > 0; ++i)
	{
		boost::intrusive_ptr<observer> o = *i;
		if (o->flags & observer::flag_alive)
		{
			TORRENT_ASSERT(o->flags & observer::flag_queried);
			TORRENT_LOG(traversal) << "[" << this << "]  "
				<< results_target
				<< " id: " << o->id()
				<< " distance: " << distance_exp(m_target, o->id())
				<< " address: " << o->target_ep()
				;
			--results_target;
			int dist = distance_exp(m_target, o->id());
			if (dist < closest_target) closest_target = dist;
		}
	}

	TORRENT_LOG(traversal) << "[" << this << "] COMPLETED "
		<< "distance: " << closest_target
		<< " type: " << name()
		;

#endif
	// delete all our references to the observer objects so
	// they will in turn release the traversal algorithm
	m_results.clear();
}

bool traversal_algorithm::add_requests()
{
	int results_target = m_num_target_nodes;

	// this only counts outstanding requests at the top of the
	// target list. This is <= m_invoke count. m_invoke_count
	// is the total number of outstanding requests, including
	// old ones that may be waiting on nodes much farther behind
	// the current point we've reached in the search.
	int outstanding = 0;

	// if we're doing aggressive lookups, we keep branch-factor
	// outstanding requests _at the tops_ of the result list. Otherwise
	// we just keep any branch-factor outstanding requests
	bool agg = m_node.settings().aggressive_lookups;

	// Find the first node that hasn't already been queried.
	// and make sure that the 'm_branch_factor' top nodes
	// stay queried at all times (obviously ignoring failed nodes)
	// and without surpassing the 'result_target' nodes (i.e. k=8)
	// this is a slight variation of the original paper which instead
	// limits the number of outstanding requests, this limits the
	// number of good outstanding requests. It will use more traffic,
	// but is intended to speed up lookups
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end
		&& results_target > 0
		&& (agg ? outstanding < m_branch_factor
			: m_invoke_count < m_branch_factor);
		++i)
	{
		observer* o = i->get();
		if (o->flags & observer::flag_alive)
		{
			TORRENT_ASSERT(o->flags & observer::flag_queried);
			--results_target;
			continue;
		}
		if (o->flags & observer::flag_queried)
		{
			// if it's queried, not alive and not failed, it
			// must be currently in flight
			if ((o->flags & observer::flag_failed) == 0)
				++outstanding;

			continue;
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << this << "] INVOKE "
			<< " nodes-left: " << (m_results.end() - i)
			<< " top-invoke-count: " << outstanding
			<< " invoke-count: " << m_invoke_count
			<< " branch-factor: " << m_branch_factor
			<< " distance: " << distance_exp(m_target, (*i)->id())
			<< " type: " << name()
			;
#endif

		o->flags |= observer::flag_queried;
		if (invoke(*i))
		{
			TORRENT_ASSERT(m_invoke_count >= 0);
			++m_invoke_count;
			++outstanding;
		}
		else
		{
			o->flags |= observer::flag_failed;
		}
	}

	// this is the completion condition. If we found m_num_target_nodes
	// (i.e. k=8) completed results, without finding any still
	// outstanding requests, we're done.
	// also, if invoke count is 0, it means we didn't even find 'k'
	// working nodes, we still have to terminate though.
	return (results_target == 0 && outstanding == 0) || m_invoke_count == 0;
}

void traversal_algorithm::add_router_entries()
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << "[" << this << "] using router nodes to initiate traversal algorithm. "
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

void traversal_observer::reply(msg const& m)
{
	lazy_entry const* r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get()
			<< "] missing response dict";
#endif
		return;
	}

	// look for nodes
	lazy_entry const* n = r->dict_find_string("nodes");
	if (n)
	{
		char const* nodes = n->string_ptr();
		char const* end = nodes + n->string_length();

		while (end - nodes >= 26)
		{
			node_id id;
			std::copy(nodes, nodes + 20, id.begin());
			nodes += 20;
			m_algorithm->traverse(id, read_v4_endpoint<udp::endpoint>(nodes));
		}
	}

	lazy_entry const* id = r->dict_find_string("id");
	if (!id || id->string_length() != 20)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get() << "] invalid id in response";
#endif
		return;
	}

	// in case we didn't know the id of this peer when we sent the message to
	// it. For instance if it's a bootstrap node.
	set_id(node_id(id->string_ptr()));
}

void traversal_algorithm::abort()
{
	m_num_target_nodes = 0;
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end; ++i)
	{
		observer& o = **i;
		if (o.flags & observer::flag_queried)
			o.flags |= observer::flag_done;
	}
	done();
}

} } // namespace libtorrent::dht

