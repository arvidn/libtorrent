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

// TODO: it would be nice to not have this dependency here
#include "libtorrent/aux_/session_impl.hpp"

#include <boost/bind.hpp>

#include <libtorrent/io.hpp>
#include <libtorrent/invariant_check.hpp>
#include <libtorrent/kademlia/node_id.hpp> // for generate_random_id
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/logging.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/time.hpp>
#include <time.h> // time()

#ifdef TORRENT_DHT_VERBOSE_LOGGING
#include <fstream>
#endif

namespace libtorrent { namespace dht
{

namespace io = libtorrent::detail;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(rpc)
#endif

void intrusive_ptr_add_ref(observer const* o)
{
	TORRENT_ASSERT(o != 0);
	TORRENT_ASSERT(o->m_refs >= 0);
	++o->m_refs;
}

void intrusive_ptr_release(observer const* o)
{
	TORRENT_ASSERT(o != 0);
	TORRENT_ASSERT(o->m_refs > 0);
	if (--o->m_refs == 0)
	{
		boost::intrusive_ptr<traversal_algorithm> ta = o->m_algorithm;
		(const_cast<observer*>(o))->~observer();
		ta->free_observer(const_cast<observer*>(o));
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
		flags |= flag_ipv6_address;
		m_addr.v6 = ep.address().to_v6().to_bytes();
	}
	else
#endif
	{
		flags &= ~flag_ipv6_address;
		m_addr.v4 = ep.address().to_v4().to_bytes();
	}
}

address observer::target_addr() const
{
#if TORRENT_USE_IPV6
	if (flags & flag_ipv6_address)
		return address_v6(m_addr.v6);
	else
#endif
		return address_v4(m_addr.v4);
}

udp::endpoint observer::target_ep() const
{
	return udp::endpoint(target_addr(), m_port);
}

void observer::abort()
{
	if (flags & flag_done) return;
	flags |= flag_done;
	m_algorithm->failed(observer_ptr(this), traversal_algorithm::prevent_request);
}

void observer::done()
{
	if (flags & flag_done) return;
	flags |= flag_done;
	m_algorithm->finished(observer_ptr(this));
}

void observer::short_timeout()
{
	if (flags & flag_short_timeout) return;
	m_algorithm->failed(observer_ptr(this), traversal_algorithm::short_timeout);
}

// this is called when no reply has been received within
// some timeout
void observer::timeout()
{
	if (flags & flag_done) return;
	flags |= flag_done;
	m_algorithm->failed(observer_ptr(this));
}

enum { observer_size = max3<
	sizeof(find_data_observer)
	, sizeof(announce_observer)
	, sizeof(null_observer)
	>::value
};

rpc_manager::rpc_manager(node_id const& our_id
	, routing_table& table, send_fun const& sf
	, void* userdata
	, external_ip_fun ext_ip)
	: m_pool_allocator(observer_size, 10)
	, m_send(sf)
	, m_userdata(userdata)
	, m_our_id(our_id)
	, m_table(table)
	, m_timer(time_now())
	, m_random_number(generate_random_id())
	, m_allocated_observers(0)
	, m_destructing(false)
	, m_ext_ip(ext_ip)
{
	std::srand(time(0));

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "Constructing";

#define PRINT_OFFSETOF(x, y) TORRENT_LOG(rpc) << "  +" << offsetof(x, y) << ": " #y

	TORRENT_LOG(rpc) << " observer: " << sizeof(observer);
	PRINT_OFFSETOF(observer, m_sent);
	PRINT_OFFSETOF(observer, m_refs);
	PRINT_OFFSETOF(observer, m_algorithm);
	PRINT_OFFSETOF(observer, m_id);
	PRINT_OFFSETOF(observer, m_addr);
	PRINT_OFFSETOF(observer, m_port);
	PRINT_OFFSETOF(observer, m_transaction_id);
	PRINT_OFFSETOF(observer, flags);

	TORRENT_LOG(rpc) << " announce_observer: " << sizeof(announce_observer);
	TORRENT_LOG(rpc) << " null_observer: " << sizeof(null_observer);
	TORRENT_LOG(rpc) << " find_data_observer: " << sizeof(find_data_observer);

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
	
	for (transactions_t::iterator i = m_transactions.begin()
		, end(m_transactions.end()); i != end; ++i)
	{
		(*i)->abort();
	}
}

void* rpc_manager::allocate_observer()
{
	m_pool_allocator.set_next_size(10);
	void* ret = m_pool_allocator.malloc();
	if (ret) ++m_allocated_observers;
	return ret;
}

void rpc_manager::free_observer(void* ptr)
{
	if (!ptr) return;
	--m_allocated_observers;
	m_pool_allocator.free(ptr);
}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
size_t rpc_manager::allocation_size() const
{
	return observer_size;
}
#endif
#ifdef TORRENT_DEBUG
void rpc_manager::check_invariant() const
{
	for (transactions_t::const_iterator i = m_transactions.begin()
		, end(m_transactions.end()); i != end; ++i)
	{
		TORRENT_ASSERT(*i);
	}
}
#endif

void rpc_manager::unreachable(udp::endpoint const& ep)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << time_now_string() << " PORT_UNREACHABLE [ ip: " << ep << " ]";
#endif

	for (transactions_t::iterator i = m_transactions.begin();
		i != m_transactions.end();)
	{
		TORRENT_ASSERT(*i);
		observer_ptr const& o = *i;
		if (o->target_ep() != ep) { ++i; continue; }
		observer_ptr ptr = *i;
		m_transactions.erase(i++);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "  found transaction [ tid: " << ptr->transaction_id() << " ]";
#endif
		ptr->timeout();
		break;
	}
}

// defined in node.cpp
void incoming_error(entry& e, char const* msg);

bool rpc_manager::incoming(msg const& m, node_id* id)
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

	for (transactions_t::iterator i = m_transactions.begin()
		, end(m_transactions.end()); i != end; ++i)
	{
		TORRENT_ASSERT(*i);
		if ((*i)->transaction_id() != tid) continue;
		if (m.addr.address() != (*i)->target_addr()) continue;
		o = *i;
		m_transactions.erase(i);
		break;
	}

	if (!o)
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

	lazy_entry const* ext_ip = ret_ent->dict_find_string("ip");
	if (ext_ip && ext_ip->string_length() == 4)
	{
		// this node claims we use the wrong node-ID!
		address_v4::bytes_type b;
		memcpy(&b[0], ext_ip->string_ptr(), 4);
		m_ext_ip(address_v4(b), aux::session_impl::source_dht, m.addr.address());
	}
#if TORRENT_USE_IPV6
	else if (ext_ip && ext_ip->string_length() == 16)
	{
		// this node claims we use the wrong node-ID!
		address_v6::bytes_type b;
		memcpy(&b[0], ext_ip->string_ptr(), 16);
		m_ext_ip(address_v6(b), aux::session_impl::source_dht, m.addr.address());
	}
#endif

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "[" << o->m_algorithm.get() << "] Reply with transaction id: " 
		<< tid << " from " << m.addr;
#endif
	o->reply(m);
	*id = node_id(node_id_ent->string_ptr());

	// we found an observer for this reply, hence the node is not spoofing
	// add it to the routing table
	return m_table.node_seen(*id, m.addr);
}

time_duration rpc_manager::tick()
{
	INVARIANT_CHECK;

	const static int short_timeout = 1;
	const static int timeout = 8;

	//	look for observers that have timed out

	if (m_transactions.empty()) return seconds(short_timeout);

	std::list<observer_ptr> timeouts;

	time_duration ret = seconds(short_timeout);
	ptime now = time_now();

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	ptime last = min_time();
	for (transactions_t::iterator i = m_transactions.begin();
		i != m_transactions.end(); ++i)
	{
		TORRENT_ASSERT((*i)->sent() >= last);
		last = (*i)->sent();
	}
#endif

	for (transactions_t::iterator i = m_transactions.begin();
		i != m_transactions.end();)
	{
		observer_ptr o = *i;

		// if we reach an observer that hasn't timed out
		// break, because every observer after this one will
		// also not have timed out yet
		time_duration diff = now - o->sent();
		if (diff < seconds(timeout))
		{
			ret = seconds(timeout) - diff;
			break;
		}
		
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(rpc) << "[" << o->m_algorithm.get() << "] Timing out transaction id: " 
			<< (*i)->transaction_id() << " from " << o->target_ep();
#endif
		m_transactions.erase(i++);
		timeouts.push_back(o);
	}
	
	std::for_each(timeouts.begin(), timeouts.end(), boost::bind(&observer::timeout, _1));
	timeouts.clear();

	for (transactions_t::iterator i = m_transactions.begin();
		i != m_transactions.end(); ++i)
	{
		observer_ptr o = *i;

		// if we reach an observer that hasn't timed out
		// break, because every observer after this one will
		// also not have timed out yet
		time_duration diff = now - o->sent();
		if (diff < seconds(short_timeout))
		{
			ret = seconds(short_timeout) - diff;
			break;
		}
		
		if (o->has_short_timeout()) continue;

		// TODO: don't call short_timeout() again if we've
		// already called it once
		timeouts.push_back(o);
	}

	std::for_each(timeouts.begin(), timeouts.end(), boost::bind(&observer::short_timeout, _1));
	
	return ret;
}

void rpc_manager::add_our_id(entry& e)
{
	e["id"] = m_our_id.to_string();
}

bool rpc_manager::invoke(entry& e, udp::endpoint target_addr
	, observer_ptr o)
{
	INVARIANT_CHECK;

	if (m_destructing) return false;

	e["y"] = "q";
	entry& a = e["a"];
	add_our_id(a);

	std::string transaction_id;
	transaction_id.resize(2);
	char* out = &transaction_id[0];
	int tid = rand() ^ (rand() << 5);
	io::write_uint16(tid, out);
	e["t"] = transaction_id;
		
	o->set_target(target_addr);
	o->set_transaction_id(tid);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(rpc) << "[" << o->m_algorithm.get() << "] invoking "
		<< e["q"].string() << " -> " << target_addr;
#endif

	if (m_send(m_userdata, e, target_addr, 1))
	{
		m_transactions.push_back(o);
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		o->m_was_sent = true;
#endif
		return true;
	}
	return false;
}

observer::~observer()
{
	// if the message was sent, it must have been
	// reported back to the traversal_algorithm as
	// well. If it wasn't sent, it cannot have been
	// reported back
	TORRENT_ASSERT(m_was_sent == bool(flags & flag_done) || m_was_abandoned);
	TORRENT_ASSERT(!m_in_constructor);
}

} } // namespace libtorrent::dht

