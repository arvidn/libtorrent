/*

Copyright (c) 2006-2016, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <deque>
#include <map>
#include <boost/cstdint.hpp>
#include <boost/pool/pool.hpp>
#include <boost/function/function3.hpp>

#if TORRENT_HAS_BOOST_UNORDERED
#include <boost/unordered_map.hpp>
#else
#include <multimap>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <libtorrent/socket.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/observer.hpp>

#include "libtorrent/time.hpp"

namespace libtorrent { namespace aux { struct session_impl; } }

namespace libtorrent { struct dht_settings; }

namespace libtorrent { namespace dht
{

struct dht_logger;
struct udp_socket_interface;

struct TORRENT_EXTRA_EXPORT null_observer : public observer
{
	null_observer(boost::intrusive_ptr<traversal_algorithm> const& a
		, udp::endpoint const& ep, node_id const& id): observer(a, ep, id) {}
	virtual void reply(msg const&) { flags |= flag_done; }
};

class routing_table;

class TORRENT_EXTRA_EXPORT rpc_manager
{
public:

	rpc_manager(node_id const& our_id
		, dht_settings const& settings
		, routing_table& table
		, udp_socket_interface* sock
		, dht_logger* log);
	~rpc_manager();

	void unreachable(udp::endpoint const& ep);

	// returns true if the node needs a refresh
	// if so, id is assigned the node id to refresh
	bool incoming(msg const&, node_id* id);
	time_duration tick();

	bool invoke(entry& e, udp::endpoint target
		, observer_ptr o);

	void add_our_id(entry& e);

#if TORRENT_USE_ASSERTS
	size_t allocation_size() const;
#endif
#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

	void* allocate_observer();
	void free_observer(void* ptr);

	int num_allocated_observers() const { return m_allocated_observers; }

private:

	boost::uint32_t calc_connection_id(udp::endpoint addr);

	mutable boost::pool<> m_pool_allocator;

#if TORRENT_HAS_BOOST_UNORDERED
	typedef boost::unordered_multimap<int, observer_ptr> transactions_t;
#else
	typedef std::multimap<int, observer_ptr> transactions_t;
#endif
	transactions_t m_transactions;

	udp_socket_interface* m_sock;
	dht_logger* m_log;
	dht_settings const& m_settings;
	routing_table& m_table;
	time_point m_timer;
	node_id m_our_id;
	boost::uint32_t m_allocated_observers:31;
	boost::uint32_t m_destructing:1;
};

} } // namespace libtorrent::dht

#endif


