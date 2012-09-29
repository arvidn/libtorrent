/*

Copyright (c) 2006, Arvid Norberg
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

#ifndef RPC_MANAGER_HPP
#define RPC_MANAGER_HPP

#include <vector>
#include <map>
#include <boost/cstdint.hpp>
#include <boost/pool/pool.hpp>
#include <boost/function/function3.hpp>

#include <libtorrent/socket.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/observer.hpp>

#include "libtorrent/ptime.hpp"

namespace libtorrent { namespace aux { struct session_impl; } }

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(rpc);
#endif

struct null_observer : public observer
{
	null_observer(boost::intrusive_ptr<traversal_algorithm> const& a
		, udp::endpoint const& ep, node_id const& id): observer(a, ep, id) {}
	virtual void reply(msg const&) { flags |= flag_done; }
};

class routing_table;

class TORRENT_EXTRA_EXPORT rpc_manager
{
public:
	typedef bool (*send_fun)(void* userdata, entry&, udp::endpoint const&, int);
	typedef boost::function3<void, address, int, address> external_ip_fun;

	rpc_manager(node_id const& our_id
		, routing_table& table, send_fun const& sf
		, void* userdata, external_ip_fun ext_ip);
	~rpc_manager();

	void unreachable(udp::endpoint const& ep);

	// returns true if the node needs a refresh
	// if so, id is assigned the node id to refresh
	bool incoming(msg const&, node_id* id);
	time_duration tick();

	bool invoke(entry& e, udp::endpoint target
		, observer_ptr o);

	void add_our_id(entry& e);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	size_t allocation_size() const;
#endif
#ifdef TORRENT_DEBUG
	void check_invariant() const;
#endif

	void* allocate_observer();
	void free_observer(void* ptr);

	int num_allocated_observers() const { return m_allocated_observers; }

private:

	boost::uint32_t calc_connection_id(udp::endpoint addr);

	mutable boost::pool<> m_pool_allocator;

	typedef std::list<observer_ptr> transactions_t;
	transactions_t m_transactions;
	
	send_fun m_send;
	void* m_userdata;
	node_id m_our_id;
	routing_table& m_table;
	ptime m_timer;
	node_id m_random_number;
	int m_allocated_observers;
	bool m_destructing;
	external_ip_fun m_ext_ip;
};

} } // namespace libtorrent::dht

#endif


