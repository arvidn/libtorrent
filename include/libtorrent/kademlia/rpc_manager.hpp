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
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/cstdint.hpp>
#include <boost/array.hpp>
#include <boost/pool/pool.hpp>

#include <libtorrent/socket.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/node_entry.hpp>
#include <libtorrent/kademlia/observer.hpp>

#include "libtorrent/time.hpp"

namespace libtorrent { namespace dht
{

struct observer;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DECLARE_LOG(rpc);
#endif

struct null_observer : public observer
{
	null_observer(boost::pool<>& allocator): observer(allocator) {}
	virtual void reply(msg const&) {}
	virtual void timeout() {}
	virtual void send(msg&) {}
	void abort() {}
};

class routing_table;

class rpc_manager
{
public:
	typedef boost::function1<void, msg const&> fun;
	typedef boost::function1<void, msg const&> send_fun;

	rpc_manager(fun const& incoming_fun, node_id const& our_id
		, routing_table& table, send_fun const& sf);
	~rpc_manager();

	void unreachable(udp::endpoint const& ep);

	// returns true if the node needs a refresh
	bool incoming(msg const&);
	time_duration tick();

	void invoke(int message_id, udp::endpoint target
		, observer_ptr o);

	void reply(msg& m);

#ifdef TORRENT_DEBUG
	size_t allocation_size() const;
	void check_invariant() const;
#endif

	boost::pool<>& allocator() const
	{ return m_pool_allocator; }

private:

	enum { max_transactions = 2048 };

	unsigned int new_transaction_id(observer_ptr o);
	void update_oldest_transaction_id();
	
	boost::uint32_t calc_connection_id(udp::endpoint addr);

	mutable boost::pool<> m_pool_allocator;

	typedef boost::array<observer_ptr, max_transactions>
		transactions_t;
	transactions_t m_transactions;
	std::vector<observer_ptr> m_aborted_transactions;
	
	// this is the next transaction id to be used
	int m_next_transaction_id;
	// this is the oldest transaction id still
	// (possibly) in use. This is the transaction
	// that will time out first, the one we are
	// waiting for to time out
	int m_oldest_transaction_id;
	
	fun m_incoming;
	send_fun m_send;
	node_id m_our_id;
	routing_table& m_table;
	ptime m_timer;
	node_id m_random_number;
	bool m_destructing;
};

} } // namespace libtorrent::dht

#endif


