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

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>

#include <boost/bind.hpp>

using boost::bind;

namespace libtorrent { namespace dht
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(traversal)
#endif

void traversal_algorithm::add_entry(node_id const& id, udp::endpoint addr, unsigned char flags)
{
	if (m_failed.find(addr) != m_failed.end()) return;

	result entry(id, addr, flags);
	if (entry.id.is_all_zeros())
	{
		entry.id = generate_id();
		entry.flags |= result::no_id;
	}

	std::vector<result>::iterator i = std::lower_bound(
		m_results.begin()
		, m_results.end()
		, entry
		, bind(
			compare_ref
			, bind(&result::id, _1)
			, bind(&result::id, _2)
			, m_target
		)
	);

	if (i == m_results.end() || i->id != id)
	{
		TORRENT_ASSERT(std::find_if(m_results.begin(), m_results.end()
			, bind(&result::id, _1) == id) == m_results.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "adding result: " << id << " " << addr;
#endif
		m_results.insert(i, entry);
	}
}

boost::pool<>& traversal_algorithm::allocator() const
{
	return m_rpc.allocator();
}

void traversal_algorithm::traverse(node_id const& id, udp::endpoint addr)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	if (id.is_all_zeros())
	TORRENT_LOG(traversal) << time_now_string() << " WARNING: node returned a list which included a node with id 0";
#endif
	add_entry(id, addr, 0);
}

void traversal_algorithm::finished(node_id const& id)
{
	--m_invoke_count;
	add_requests();
	if (m_invoke_count == 0) done();
}

// prevent request means that the total number of requests has
// overflown. This query failed because it was the oldest one.
// So, if this is true, don't make another request
void traversal_algorithm::failed(node_id const& id, bool prevent_request)
{
	m_invoke_count--;

	TORRENT_ASSERT(!id.is_all_zeros());
	std::vector<result>::iterator i = std::find_if(
		m_results.begin()
		, m_results.end()
		, bind(
			std::equal_to<node_id>()
			, bind(&result::id, _1)
			, id
		)
	);

	TORRENT_ASSERT(i != m_results.end());

	if (i != m_results.end())
	{
		TORRENT_ASSERT(i->flags & result::queried);
		m_failed.insert(i->addr);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "failed: " << i->id << " " << i->addr;
#endif
		// don't tell the routing table about
		// node ids that we just generated ourself
		if ((i->flags & result::no_id) == 0)
			m_table.node_failed(id);
		m_results.erase(i);
	}
	if (prevent_request)
	{
		--m_branch_factor;
		if (m_branch_factor <= 0) m_branch_factor = 1;
	}
	add_requests();
	if (m_invoke_count == 0) done();
}

namespace
{
	bool bitwise_nand(unsigned char lhs, unsigned char rhs)
	{
		return (lhs & rhs) == 0;
	}
}

void traversal_algorithm::add_requests()
{
	while (m_invoke_count < m_branch_factor)
	{
		// Find the first node that hasn't already been queried.
		// TODO: Better heuristic
		std::vector<result>::iterator i = std::find_if(
			m_results.begin()
			, last_iterator()
			, bind(
				&bitwise_nand
				, bind(&result::flags, _1)
				, (unsigned char)result::queried
			)
		);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "nodes left (" << this << "): " << (last_iterator() - i);
#endif

		if (i == last_iterator()) break;

		try
		{
			invoke(i->id, i->addr);
			++m_invoke_count;
			i->flags |= result::queried;
		}
		catch (std::exception& e) {}
	}
}

std::vector<traversal_algorithm::result>::iterator traversal_algorithm::last_iterator()
{
	return (int)m_results.size() >= m_max_results ?
		m_results.begin() + m_max_results
		: m_results.end();
}

} } // namespace libtorrent::dht

