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

#include "libtorrent/pch.hpp"
#include "libtorrent/socket.hpp"

#include <boost/bind.hpp>
#include <boost/mpl/max_element.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/sizeof.hpp>
#include <boost/mpl/transform_view.hpp>
#include <boost/mpl/deref.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/invariant_check.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/closest_nodes.hpp>
#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/hasher.hpp>

#include <fstream>

using boost::shared_ptr;

namespace libtorrent { namespace dht
{

namespace io = libtorrent::detail;
namespace mpl = boost::mpl;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(rpc)
#endif

void intrusive_ptr_add_ref(observer const* o)
{
	TORRENT_ASSERT(o->m_refs >= 0);
	TORRENT_ASSERT(o != 0);
	++o->m_refs;
}

void intrusive_ptr_release(observer const* o)
{
	TORRENT_ASSERT(o->m_refs > 0);
	TORRENT_ASSERT(o != 0);
	if (--o->m_refs == 0)
	{
		boost::pool<>& p = o->pool_allocator;
		(const_cast<observer*>(o))->~observer();
		p.free(const_cast<observer*>(o));
	}
}

node_id generate_id();

typedef mpl::vector<
	closest_nodes_observer
	, find_data_observer
	, announce_observer
	, refresh_observer
	, ping_observer
	, null_observer
	> observer_types;

typedef mpl::max_element<
	mpl::transform_view<observer_types, mpl::sizeof_<mpl::_1> >
    >::type max_observer_type_iter;

rpc_manager::rpc_manager(fun const& f, node_id const& our_id
	, routing_table& table, send_fun const& sf)
	: m_pool_allocator(sizeof(mpl::deref<max_observer_type_iter::base>::type), 10)
	, m_next_transaction_id(std::rand() % max_transactions)
	, m_oldest_transaction_id(m_next_transaction_id)
	, m_incoming(f)
	, m_send(sf)
	, m_our_id(our_id)
	, m_table(table)
	, m_timer(time_now())
	, m_random_number(generate_id())
	, m_destructing(false)
{
	std::srand(time(0));

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Constructing";
	TORRENT_LOG(rpc) << " closest_nodes_observer: " << sizeof(closest_nodes_observer);
	TORRENT_LOG(rpc) << " find_data_observer: " << sizeof(find_data_observer);
	TORRENT_LOG(rpc) << " announce_observer: " << sizeof(announce_observer);
	TORRENT_LOG(rpc) << " refresh_observer: " << sizeof(refresh_observer);
	TORRENT_LOG(rpc) << " ping_observer: " << sizeof(ping_observer);
	TORRENT_LOG(rpc) << " null_observer: " << sizeof(null_observer);
#endif
}

rpc_manager::~rpc_manager()
{
	TORRENT_ASSERT(!m_destructing);
	m_destructing = true;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Destructing";
#endif
	std::for_each(m_aborted_transactions.begin(), m_aborted_transactions.end()
		, bind(&observer::abort, _1));
	
	for (transactions_t::iterator i = m_transactions.begin()
		, end(m_transactions.end()); i != end; ++i)
	{
		if (*i) (*i)->abort();
	}
}

#ifdef TORRENT_DEBUG
size_t rpc_manager::allocation_size() const
{
	size_t s = sizeof(mpl::deref<max_observer_type_iter::base>::type);
	return s;
}

void rpc_manager::check_invariant() const
{
	TORRENT_ASSERT(m_oldest_transaction_id >= 0);
	TORRENT_ASSERT(m_oldest_transaction_id < max_transactions);
	TORRENT_ASSERT(m_next_transaction_id >= 0);
	TORRENT_ASSERT(m_next_transaction_id < max_transactions);
	TORRENT_ASSERT(!m_transactions[m_next_transaction_id]);

	for (int i = (m_next_transaction_id + 1) % max_transactions;
		i != m_oldest_transaction_id; i = (i + 1) % max_transactions)
	{
		TORRENT_ASSERT(!m_transactions[i]);
	}
}
#endif

void rpc_manager::unreachable(udp::endpoint const& ep)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << time_now_string() << " PORT_UNREACHABLE [ ip: " << ep << " ]";
#endif
	int num_active = m_oldest_transaction_id < m_next_transaction_id
		? m_next_transaction_id - m_oldest_transaction_id
		: max_transactions - m_oldest_transaction_id + m_next_transaction_id;
	TORRENT_ASSERT((m_oldest_transaction_id + num_active) % max_transactions
		== m_next_transaction_id);
	int tid = m_oldest_transaction_id;
	for (int i = 0; i < num_active; ++i, ++tid)
	{
		if (tid >= max_transactions) tid = 0;
		observer_ptr const& o = m_transactions[tid];
		if (!o) continue;
		if (o->target_ep() != ep) continue;
		observer_ptr ptr = m_transactions[tid];
		m_transactions[tid] = 0;
		if (tid == m_oldest_transaction_id)
		{
			++m_oldest_transaction_id;
			if (m_oldest_transaction_id >= max_transactions)
				m_oldest_transaction_id = 0;
		}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "  found transaction [ tid: " << tid << " ]";
#endif
		ptr->timeout();
		return;
	}
}

bool rpc_manager::incoming(msg const& m)
{
	INVARIANT_CHECK;

	if (m_destructing) return false;

	if (m.reply)
	{
		// if we don't have the transaction id in our
		// request list, ignore the packet

		if (m.transaction_id.size() < 2)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with invalid transaction id size: " 
				<< m.transaction_id.size() << " from " << m.addr;
#endif
			msg reply;
			reply.reply = true;
			reply.message_id = messages::error;
			reply.error_code = 203; // Protocol error
			char msg[100];
			snprintf(msg, sizeof(msg), "reply with invaliud transaction "
				"id, size %d", int(m.transaction_id.size()));
			reply.addr = m.addr;
			reply.transaction_id = "";
			m_send(reply);
			return false;
		}
	
		std::string::const_iterator i = m.transaction_id.begin();	
		int tid = io::read_uint16(i);

		if (tid >= (int)m_transactions.size()
			|| tid < 0)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with invalid transaction id: " 
				<< tid << " from " << m.addr;
#endif
			msg reply;
			reply.reply = true;
			reply.message_id = messages::error;
			reply.error_code = 203; // Protocol error
			reply.error_msg = "reply with invalid transaction id";
			reply.addr = m.addr;
			reply.transaction_id = "";
			m_send(reply);
			return false;
		}
		
		observer_ptr o = m_transactions[tid];

		if (!o)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with unknown transaction id: " 
				<< tid << " from " << m.addr << " (possibly timed out)";
#endif
			return false;
		}
		
		if (m.addr.address() != o->target_addr)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Reply with incorrect address and valid transaction id: " 
				<< tid << " from " << m.addr << " expected: " << o->target_addr;
#endif
			return false;
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		std::ofstream reply_stats("round_trip_ms.log", std::ios::app);
		reply_stats << m.addr << "\t" << total_milliseconds(time_now() - o->sent)
			<< std::endl;
#endif
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "Reply with transaction id: " 
			<< tid << " from " << m.addr;
#endif
		o->reply(m);
		m_transactions[tid] = 0;
		return m_table.node_seen(m.id, m.addr);
	}
	else
	{
		TORRENT_ASSERT(m.message_id != messages::error);
		// this is an incoming request
		m_incoming(m);
	}
	return false;
}

time_duration rpc_manager::tick()
{
	INVARIANT_CHECK;

	const int timeout_ms = 10 * 1000;

	//	look for observers that has timed out

	if (m_next_transaction_id == m_oldest_transaction_id) return milliseconds(timeout_ms);

	std::vector<observer_ptr> timeouts;

	time_duration ret = milliseconds(timeout_ms);

	for (;m_next_transaction_id != m_oldest_transaction_id;
		m_oldest_transaction_id = (m_oldest_transaction_id + 1) % max_transactions)
	{
		TORRENT_ASSERT(m_oldest_transaction_id >= 0);
		TORRENT_ASSERT(m_oldest_transaction_id < max_transactions);

		observer_ptr o = m_transactions[m_oldest_transaction_id];
		if (!o) continue;

		time_duration diff = o->sent + milliseconds(timeout_ms) - time_now();
		if (diff > seconds(0))
		{
			if (diff < seconds(1))
			{
				ret = seconds(1);
				break;
			}
			else
			{
				ret = diff;
				break;	
			}
		}
		
#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			m_transactions[m_oldest_transaction_id] = 0;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(rpc) << "Timing out transaction id: " 
				<< m_oldest_transaction_id << " from " << o->target_ep();
#endif
			timeouts.push_back(o);
#ifndef BOOST_NO_EXCEPTIONS
		} catch (std::exception) {}
#endif
	}
	
	std::for_each(timeouts.begin(), timeouts.end(), boost::bind(&observer::timeout, _1));
	timeouts.clear();
	
	// clear the aborted transactions, will likely
	// generate new requests. We need to swap, since the
	// destrutors may add more observers to the m_aborted_transactions
	std::vector<observer_ptr>().swap(m_aborted_transactions);
	return ret;
}

unsigned int rpc_manager::new_transaction_id(observer_ptr o)
{
	INVARIANT_CHECK;

	unsigned int tid = m_next_transaction_id;
	m_next_transaction_id = (m_next_transaction_id + 1) % max_transactions;
	if (m_transactions[m_next_transaction_id])
	{
		// moving the observer into the set of aborted transactions
		// it will prevent it from spawning new requests right now,
		// since that would break the invariant
		observer_ptr o = m_transactions[m_next_transaction_id];
		m_aborted_transactions.push_back(o);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "[new_transaction_id] Aborting message with transaction id: " 
			<< m_next_transaction_id << " sent to " << o->target_ep()
			<< " " << total_seconds(time_now() - o->sent) << " seconds ago";
#endif
		m_transactions[m_next_transaction_id] = 0;
		TORRENT_ASSERT(m_oldest_transaction_id == m_next_transaction_id);
	}
	TORRENT_ASSERT(!m_transactions[tid]);
	m_transactions[tid] = o;
	if (m_oldest_transaction_id == m_next_transaction_id)
	{
		m_oldest_transaction_id = (m_oldest_transaction_id + 1) % max_transactions;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "WARNING: transaction limit reached! Too many concurrent"
			" messages! limit: " << (int)max_transactions;
#endif
		update_oldest_transaction_id();
	}

	return tid;
}

void rpc_manager::update_oldest_transaction_id()
{
	INVARIANT_CHECK;

	TORRENT_ASSERT(m_oldest_transaction_id != m_next_transaction_id);
	while (!m_transactions[m_oldest_transaction_id])
	{
		m_oldest_transaction_id = (m_oldest_transaction_id + 1)
			% max_transactions;
		if (m_oldest_transaction_id == m_next_transaction_id)
			break;
	}
}

void rpc_manager::invoke(int message_id, udp::endpoint target_addr
	, observer_ptr o)
{
	INVARIANT_CHECK;

	if (m_destructing)
	{
		o->abort();
		return;
	}

	msg m;
	m.message_id = message_id;
	m.reply = false;
	m.id = m_our_id;
	m.addr = target_addr;
	TORRENT_ASSERT(!m_transactions[m_next_transaction_id]);
#ifdef TORRENT_DEBUG
	int potential_new_id = m_next_transaction_id;
#endif
#ifndef BOOST_NO_EXCEPTIONS
	try
	{
#endif
		m.transaction_id.clear();
		std::back_insert_iterator<std::string> out(m.transaction_id);
		io::write_uint16(m_next_transaction_id, out);
		
		o->send(m);

		o->sent = time_now();
#if TORRENT_USE_IPV6
		o->target_addr = target_addr.address();
#else
		o->target_addr = target_addr.address().to_v4();
#endif
		o->port = target_addr.port();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "Invoking " << messages::ids[message_id] 
			<< " -> " << target_addr;
#endif	
		m_send(m);
		new_transaction_id(o);
#ifndef BOOST_NO_EXCEPTIONS
	}
	catch (std::exception& e)
	{
		// m_send may fail with "no route to host"
		TORRENT_ASSERT(potential_new_id == m_next_transaction_id);
		o->abort();
	}
#endif
}

void rpc_manager::reply(msg& m)
{
	INVARIANT_CHECK;

	if (m_destructing) return;

	TORRENT_ASSERT(m.reply);
	m.id = m_our_id;
	
	m_send(m);
}

} } // namespace libtorrent::dht

