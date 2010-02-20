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
	void finished(udp::endpoint const& ep);

	enum flags_t { prevent_request = 1, short_timeout = 2 };
	void failed(udp::endpoint const& ep, int flags = 0);
	virtual ~traversal_algorithm();
	boost::pool<>& allocator() const;
	void status(dht_lookup& l);

	virtual char const* name() const { return "traversal_algorithm"; }
	virtual void start();

	node_id const& target() const { return m_target; }

	void add_entry(node_id const& id, udp::endpoint addr, unsigned char flags);

	struct result
	{
		result(node_id const& id, udp::endpoint ep, unsigned char f = 0) 
			: id(id), flags(f)
		{
#if TORRENT_USE_IPV6
			if (ep.address().is_v6())
			{
				flags |= ipv6_address;
				addr.v6 = ep.address().to_v6().to_bytes();
			}
			else
#endif
			{
				flags &= ~ipv6_address;
				addr.v4 = ep.address().to_v4().to_bytes();
			}
			port = ep.port();
		}

		udp::endpoint endpoint() const
		{
#if TORRENT_USE_IPV6
			if (flags & ipv6_address)
				return udp::endpoint(address_v6(addr.v6), port);
			else
#endif
				return udp::endpoint(address_v4(addr.v4), port);
		}

		node_id id;

		union addr_t
		{
			address_v4::bytes_type v4;
#if TORRENT_USE_IPV6
			address_v6::bytes_type v6;
#endif
		} addr;

		boost::uint16_t port;

		enum {
			queried = 1,
			initial = 2,
			no_id = 4,
			short_timeout = 8,
			failed = 16,
			ipv6_address = 32,
			alive = 64
		};
		unsigned char flags;
	};

	traversal_algorithm(
		node_impl& node
		, node_id target)
		: m_ref_count(0)
		, m_node(node)
		, m_target(target)
		, m_invoke_count(0)
		, m_branch_factor(3)
		, m_responses(0)
		, m_timeouts(0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << " [" << this << "] new traversal process. Target: " << target;
#endif
	}

protected:

	void add_requests();
	void add_router_entries();
	void init();

	virtual void done() {}
	virtual bool invoke(udp::endpoint addr) { return false; }

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

	node_impl& m_node;
	node_id m_target;
	std::vector<result> m_results;
	int m_invoke_count;
	int m_branch_factor;
	int m_responses;
	int m_timeouts;
};

} } // namespace libtorrent::dht

#endif // TRAVERSAL_ALGORITHM_050324_HPP

