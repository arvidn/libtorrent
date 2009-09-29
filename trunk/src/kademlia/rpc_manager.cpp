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
#include <boost/lexical_cast.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/invariant_check.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/hasher.hpp>

#include <fstream>

using boost::shared_ptr;
using boost::bind;

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

void observer::set_target(udp::endpoint const& ep)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	// use high resolution timers for logging
	m_sent = time_now_hires();
#else
	m_sent = time_now();
#endif

	m_port = ep.port();
#if TORRENT_USE_IPV6
	if (ep.address().is_v6())
	{
		m_is_v6 = true;
		m_addr.v6 = ep.address().to_v6().to_bytes();
	}
	else
#endif
	{
		m_is_v6 = false;
		m_addr.v4 = ep.address().to_v4().to_bytes();
	}
}

address observer::target_addr() const
{
	if (m_is_v6)
		return address_v6(m_addr.v6);
	else
		return address_v4(m_addr.v4);
}

udp::endpoint observer::target_ep() const
{
	return udp::endpoint(target_addr(), m_port);
}


node_id generate_id();

typedef mpl::vector<
	find_data_observer
	, announce_observer
	, null_observer
	> observer_types;

typedef mpl::max_element<
	mpl::transform_view<observer_types, mpl::sizeof_<mpl::_1> >
    >::type max_observer_type_iter;

rpc_manager::rpc_manager(node_id const& our_id
	, routing_table& table, send_fun const& sf
	, void* userdata)
	: m_pool_allocator(sizeof(mpl::deref<max_observer_type_iter::base>::type), 10)
	, m_next_transaction_id(std::rand() % max_transactions)
	, m_oldest_transaction_id(m_next_transaction_id)
	, m_send(sf)
	, m_userdata(userdata)
	, m_our_id(our_id)
	, m_table(table)
	, m_timer(time_now())
	, m_random_number(generate_id())
	, m_destructing(false)
{
	std::srand(time(0));

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Constructing";
	TORRENT_LOG(rpc) << " announce_observer: " << sizeof(announce_observer);
	TORRENT_LOG(rpc) << " null_observer: " << sizeof(null_observer);

#define PRINT_OFFSETOF(x, y) TORRENT_LOG(rpc) << "  +" << offsetof(x, y) << ": " #y

	TORRENT_LOG(rpc) << " observer: " << sizeof(observer);
	PRINT_OFFSETOF(observer, pool_allocator);
	PRINT_OFFSETOF(observer, m_sent);
	PRINT_OFFSETOF(observer, m_refs);
	PRINT_OFFSETOF(observer, m_addr);
	PRINT_OFFSETOF(observer, m_port);

	TORRENT_LOG(rpc) << " find_data_observer: " << sizeof(find_data_observer);
	PRINT_OFFSETOF(find_data_observer, m_algorithm);
	PRINT_OFFSETOF(find_data_observer, m_self);

#undef PRINT_OFFSETOF
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

// defined in node.cpp
void incoming_error(entry& e, char const* msg);

bool rpc_manager::incoming(msg const& m)
{
	INVARIANT_CHECK;

	if (m_destructing) return false;

	// we only deal with replies, not queries
	TORRENT_ASSERT(m.message.dict_find_string_value("y") == "r");

	// if we don't have the transaction id in our
	// request list, ignore the packet

	std::string transaction_id = m.message.dict_find_string_value("t");

	std::string::const_iterator i = transaction_id.begin();	
	int tid = transaction_id.size() != 2 ? -1 : io::read_uint16(i);

	observer_ptr o;

	if (tid >= (int)m_transactions.size() || tid < 0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "Reply with invalid transaction id size: " 
			<< transaction_id.size() << " from " << m.addr;
#endif
		entry e;
		incoming_error(e, "invalid transaction id");
		m_send(m_userdata, e, m.addr, 0);
		return false;
	}

	o = m_transactions[tid];

	if (!o)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "Reply to a timed out request " 
			<< tid << " from " << m.addr;
#endif
		return false;
	}

	if (m.addr.address() != o->target_addr())
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "Reply with incorrect address and valid transaction id: " 
			<< tid << " from " << m.addr << " expected: " << o->target_addr();
#endif
		return false;
	}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	std::ofstream reply_stats("round_trip_ms.log", std::ios::app);
	reply_stats << m.addr << "\t" << total_milliseconds(time_now_hires() - o->sent())
		<< std::endl;
#endif

	lazy_entry const* ret_ent = m.message.dict_find_dict("r");
	if (ret_ent == 0)
	{
		entry e;
		incoming_error(e, "missing 'r' key");
		m_send(m_userdata, e, m.addr, 0);
		return false;
	}

	lazy_entry const* node_id_ent = ret_ent->dict_find_string("id");
	if (node_id_ent == 0 || node_id_ent->string_length() != 20)
	{
		entry e;
		incoming_error(e, "missing 'id' key");
		m_send(m_userdata, e, m.addr, 0);
		return false;
	}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Reply with transaction id: " 
		<< tid << " from " << m.addr;
#endif
	o->reply(m);
	m_transactions[tid] = 0;
	return m_table.node_seen(node_id(node_id_ent->string_ptr()), m.addr);
}

time_duration rpc_manager::tick()
{
	INVARIANT_CHECK;

	const static int short_timeout = 2;
	const static int timeout = 10;

	//	look for observers that have timed out

	if (m_next_transaction_id == m_oldest_transaction_id) return seconds(short_timeout);

	std::vector<observer_ptr> timeouts;

	time_duration ret = seconds(short_timeout);
	ptime now = time_now();

	for (;m_next_transaction_id != m_oldest_transaction_id;
		m_oldest_transaction_id = (m_oldest_transaction_id + 1) % max_transactions)
	{
		TORRENT_ASSERT(m_oldest_transaction_id >= 0);
		TORRENT_ASSERT(m_oldest_transaction_id < max_transactions);

		observer_ptr o = m_transactions[m_oldest_transaction_id];
		if (!o) continue;

		// if we reach an observer that hasn't timed out
		// break, because every observer after this one will
		// also not have timed out yet
		time_duration diff = now - o->sent();
		if (diff < seconds(timeout))
		{
			ret = seconds(timeout) - diff;
			break;
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
	
	std::for_each(timeouts.begin(), timeouts.end(), bind(&observer::timeout, _1));
	timeouts.clear();

	// clear the aborted transactions, will likely
	// generate new requests. We need to swap, since the
	// destrutors may add more observers to the m_aborted_transactions
	std::vector<observer_ptr>().swap(m_aborted_transactions);

	for (int i = m_oldest_transaction_id; i != m_next_transaction_id;
		i = (i + 1) % max_transactions)
	{
		observer_ptr o = m_transactions[i];
		if (!o) continue;

		// if we reach an observer that hasn't timed out
		// break, because every observer after this one will
		// also not have timed out yet
		time_duration diff = now - o->sent();
		if (diff < seconds(short_timeout))
		{
			ret = seconds(short_timeout) - diff;
			break;
		}
		
		// TODO: don't call short_timeout() again if we've
		// already called it once
		timeouts.push_back(o);
	}

	std::for_each(timeouts.begin(), timeouts.end(), bind(&observer::short_timeout, _1));
	
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
			<< " " << total_seconds(time_now() - o->sent()) << " seconds ago";
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

void rpc_manager::add_our_id(entry& e)
{
	e["id"] = m_our_id.to_string();
}

void rpc_manager::invoke(entry& e, udp::endpoint target_addr
	, observer_ptr o)
{
	INVARIANT_CHECK;

	if (m_destructing)
	{
		o->abort();
		return;
	}

	e["y"] = "q";
	entry& a = e["a"];
	add_our_id(a);

	TORRENT_ASSERT(!m_transactions[m_next_transaction_id]);
#ifdef TORRENT_DEBUG
	int potential_new_id = m_next_transaction_id;
#endif
#ifndef BOOST_NO_EXCEPTIONS
	try
	{
#endif
		std::string transaction_id;
		transaction_id.resize(2);
		char* out = &transaction_id[0];
		io::write_uint16(m_next_transaction_id, out);
		e["t"] = transaction_id;
		
		o->set_target(target_addr);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "Invoking " << e["q"].string() << " -> " << target_addr;
#endif	
		m_send(m_userdata, e, target_addr, 1);
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
/*
void rpc_manager::reply(msg& m)
{
	INVARIANT_CHECK;

	if (m_destructing) return;

	TORRENT_ASSERT(m.reply);
	m.id = m_our_id;
	
	m_send(m);
}
*/
} } // namespace libtorrent::dht

