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

#ifndef TRAVERSAL_ALGORITHM_050324_HPP
#define TRAVERSAL_ALGORITHM_050324_HPP

#include <vector>
#include <set>

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/logging.hpp>

#include <boost/noncopyable.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/pool/pool.hpp>

namespace libtorrent { namespace dht
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(traversal);
#endif

class rpc_manager;

// this class may not be instantiated as a stack object
class traversal_algorithm : boost::noncopyable
{
public:
	void traverse(node_id const& id, udp::endpoint addr);
	void finished(node_id const& id);
	void failed(node_id const& id, bool prevent_request = false);
	virtual ~traversal_algorithm() {}
	boost::pool<>& allocator() const;

protected:
	template<class InIt>
	traversal_algorithm(
		node_id target
		, int branch_factor
		, int max_results
		, routing_table& table
		, rpc_manager& rpc
		, InIt start
		, InIt end
	);

	void add_requests();
	void add_entry(node_id const& id, udp::endpoint addr, unsigned char flags);

	virtual void done() = 0;
	virtual void invoke(node_id const& id, udp::endpoint addr) = 0;

	struct result
	{
		result(node_id const& id, udp::endpoint addr, unsigned char f = 0) 
			: id(id), addr(addr), flags(f) {}

		node_id id;
		udp::endpoint addr;
		enum { queried = 1, initial = 2, no_id = 4 };
		unsigned char flags;
	};

	std::vector<result>::iterator last_iterator();

	friend void intrusive_ptr_add_ref(traversal_algorithm* p)
	{
		p->m_ref_count++;
	}

	friend void intrusive_ptr_release(traversal_algorithm* p)
	{
		if (--p->m_ref_count == 0)
			delete p;
	}

	int m_ref_count;

	node_id m_target;
	int m_branch_factor;
	int m_max_results;
	std::vector<result> m_results;
	std::set<udp::endpoint> m_failed;
	routing_table& m_table;
	rpc_manager& m_rpc;
	int m_invoke_count;
};

template<class InIt>
traversal_algorithm::traversal_algorithm(
	node_id target
	, int branch_factor
	, int max_results
	, routing_table& table
	, rpc_manager& rpc
	, InIt start	// <- nodes to initiate traversal with
	, InIt end
)
	: m_ref_count(0)
	, m_target(target)
	, m_branch_factor(branch_factor)
	, m_max_results(max_results)
	, m_table(table)
	, m_rpc(rpc)
	, m_invoke_count(0)
{
	using boost::bind;

	for (InIt i = start; i != end; ++i)
	{
		add_entry(i->id, i->addr, result::initial);
	}
	
	// in case the routing table is empty, use the
	// router nodes in the table
	if (start == end)
	{
		for (routing_table::router_iterator i = table.router_begin()
			, end(table.router_end()); i != end; ++i)
		{
			add_entry(node_id(0), *i, result::initial);
		}
	}

}

} } // namespace libtorrent::dht

#endif // TRAVERSAL_ALGORITHM_050324_HPP

