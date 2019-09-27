/*

Copyright (c) 2006-2018, Arvid Norberg
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

#include <libtorrent/config.hpp>
#include <libtorrent/io.hpp>
#include <libtorrent/random.hpp>
#include <libtorrent/invariant_check.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/put_data.hpp>
#include <libtorrent/kademlia/refresh.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/kademlia/direct_request.hpp>
#include <libtorrent/kademlia/get_item.hpp>
#include <libtorrent/kademlia/sample_infohashes.hpp>
#include <libtorrent/kademlia/dht_settings.hpp>

#include <libtorrent/socket_io.hpp> // for print_endpoint
#include <libtorrent/aux_/time.hpp> // for aux::time_now
#include <libtorrent/aux_/aligned_union.hpp>
#include <libtorrent/broadcast_socket.hpp> // for is_v6

#include <type_traits>
#include <functional>

#ifndef TORRENT_DISABLE_LOGGING
#include <cinttypes> // for PRId64 et.al.
#endif

using namespace std::placeholders;

namespace libtorrent { namespace dht {

// TODO: 3 move this into it's own .cpp file

constexpr observer_flags_t observer::flag_queried;
constexpr observer_flags_t observer::flag_initial;
constexpr observer_flags_t observer::flag_no_id;
constexpr observer_flags_t observer::flag_short_timeout;
constexpr observer_flags_t observer::flag_failed;
constexpr observer_flags_t observer::flag_ipv6_address;
constexpr observer_flags_t observer::flag_alive;
constexpr observer_flags_t observer::flag_done;

dht_observer* observer::get_observer() const
{
	return m_algorithm->get_node().observer();
}

void observer::set_target(udp::endpoint const& ep)
{
	m_sent = clock_type::now();

	m_port = ep.port();
	if (is_v6(ep))
	{
		flags |= flag_ipv6_address;
		m_addr.v6 = ep.address().to_v6().to_bytes();
	}
	else
	{
		flags &= ~flag_ipv6_address;
		m_addr.v4 = ep.address().to_v4().to_bytes();
	}
}

address observer::target_addr() const
{
	if (flags & flag_ipv6_address)
		return address_v6(m_addr.v6);
	else
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
	m_algorithm->failed(self(), traversal_algorithm::prevent_request);
}

void observer::done()
{
	if (flags & flag_done) return;
	flags |= flag_done;
	m_algorithm->finished(self());
}

void observer::short_timeout()
{
	if (flags & flag_short_timeout) return;
	m_algorithm->failed(self(), traversal_algorithm::short_timeout);
}

void observer::timeout()
{
	if (flags & flag_done) return;
	flags |= flag_done;
	m_algorithm->failed(self());
}

void observer::set_id(node_id const& id)
{
	if (m_id == id) return;
	m_id = id;
	if (m_algorithm) m_algorithm->resort_result(this);
}

using observer_storage = aux::aligned_union<1
	, find_data_observer
	, announce_observer
	, put_data_observer
	, direct_observer
	, get_item_observer
	, get_peers_observer
	, obfuscated_get_peers_observer
	, sample_infohashes_observer
	, null_observer
	, traversal_observer>::type;

rpc_manager::rpc_manager(node_id const& our_id
	, dht_settings const& settings
	, routing_table& table
	, aux::listen_socket_handle const& sock
	, socket_manager* sock_man
	, dht_logger* log)
	: m_pool_allocator(sizeof(observer_storage), 10)
	, m_sock(sock)
	, m_sock_man(sock_man)
#ifndef TORRENT_DISABLE_LOGGING
	, m_log(log)
#endif
	, m_settings(settings)
	, m_table(table)
	, m_our_id(our_id)
	, m_allocated_observers(0)
	, m_destructing(false)
{
#ifdef TORRENT_DISABLE_LOGGING
	TORRENT_UNUSED(log);
#endif
}

rpc_manager::~rpc_manager()
{
	TORRENT_ASSERT(!m_destructing);
	m_destructing = true;

	for (auto const& t : m_transactions)
	{
		t.second->abort();
	}
}

void* rpc_manager::allocate_observer()
{
	m_pool_allocator.set_next_size(10);
	void* ret = m_pool_allocator.malloc();
	if (ret != nullptr) ++m_allocated_observers;
	return ret;
}

void rpc_manager::free_observer(void* ptr)
{
	if (ptr == nullptr) return;
	--m_allocated_observers;
	m_pool_allocator.free(ptr);
}

#if TORRENT_USE_ASSERTS
size_t rpc_manager::allocation_size() const
{
	return sizeof(observer_storage);
}
#endif
#if TORRENT_USE_INVARIANT_CHECKS
void rpc_manager::check_invariant() const
{
	for (auto const& t : m_transactions)
	{
		TORRENT_ASSERT(t.second);
	}
}
#endif

void rpc_manager::unreachable(udp::endpoint const& ep)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_log->should_log(dht_logger::rpc_manager))
	{
		m_log->log(dht_logger::rpc_manager, "PORT_UNREACHABLE [ ip: %s ]"
			, print_endpoint(ep).c_str());
	}
#endif

	for (auto i = m_transactions.begin(); i != m_transactions.end();)
	{
		TORRENT_ASSERT(i->second);
		if (i->second->target_ep() != ep) { ++i; continue; }
		observer_ptr o = i->second;
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log(dht_logger::rpc_manager, "[%u] found transaction [ tid: %d ]"
			, o->algorithm()->id(), i->first);
#endif
		i = m_transactions.erase(i);
		o->timeout();
		break;
	}
}

bool rpc_manager::incoming(msg const& m, node_id* id)
{
	INVARIANT_CHECK;

	if (m_destructing) return false;

	// we only deal with replies and errors, not queries
	TORRENT_ASSERT(m.message.dict_find_string_value("y") == "r"
		|| m.message.dict_find_string_value("y") == "e");

	// if we don't have the transaction id in our
	// request list, ignore the packet

	auto transaction_id = m.message.dict_find_string_value("t");
	if (transaction_id.empty()) return false;

	auto ptr = transaction_id.begin();
	int tid = transaction_id.size() != 2 ? -1 : detail::read_uint16(ptr);

	observer_ptr o;
	auto range = m_transactions.equal_range(tid);
	for (auto i = range.first; i != range.second; ++i)
	{
		if (m.addr.address() != i->second->target_addr()) continue;
		o = i->second;
		i = m_transactions.erase(i);
		break;
	}

	if (!o)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (m_table.native_endpoint(m.addr) && m_log->should_log(dht_logger::rpc_manager))
		{
			m_log->log(dht_logger::rpc_manager, "reply with unknown transaction id size: %d from %s"
				, int(transaction_id.size()), print_endpoint(m.addr).c_str());
		}
#endif
		// this isn't necessarily because the other end is doing
		// something wrong. This can also happen when we restart
		// the node, and we prematurely abort all outstanding
		// requests. Also, this opens up a potential magnification
		// attack.
//		entry e;
//		incoming_error(e, "invalid transaction id");
//		m_sock->send_packet(e, m.addr);
		return false;
	}

	time_point const now = clock_type::now();

#ifndef TORRENT_DISABLE_LOGGING
	if (m_log->should_log(dht_logger::rpc_manager))
	{
		m_log->log(dht_logger::rpc_manager, "[%u] round trip time(ms): %" PRId64 " from %s"
			, o->algorithm()->id(), total_milliseconds(now - o->sent())
			, print_endpoint(m.addr).c_str());
	}
#endif

	if (m.message.dict_find_string_value("y") == "e")
	{
		// It's an error.
#ifndef TORRENT_DISABLE_LOGGING
		if (m_log->should_log(dht_logger::rpc_manager))
		{
			bdecode_node err = m.message.dict_find_list("e");
			if (err && err.list_size() >= 2
				&& err.list_at(0).type() == bdecode_node::int_t
				&& err.list_at(1).type() == bdecode_node::string_t)
			{
				m_log->log(dht_logger::rpc_manager, "[%u] reply with error from %s: (%" PRId64 ") %s"
					, o->algorithm()->id()
					, print_endpoint(m.addr).c_str()
					, err.list_int_value_at(0)
					, err.list_string_value_at(1).to_string().c_str());
			}
			else
			{
				m_log->log(dht_logger::rpc_manager, "[%u] reply with (malformed) error from %s"
					, o->algorithm()->id(), print_endpoint(m.addr).c_str());
			}
		}
#endif
		// Logically, we should call o->reply(m) since we get a reply.
		// a reply could be "response" or "error", here the reply is an "error".
		// if the reply is an "error", basically the observer could/will
		// do nothing with it, especially when observer::reply() is intended to
		// handle a "response", not an "error".
		// A "response" should somehow call algorithm->finished(), and an error/timeout
		// should call algorithm->failed(). From this point of view,
		// we should call o->timeout() instead of o->reply(m) because o->reply()
		// will call algorithm->finished().
		o->timeout();
		return false;
	}

	bdecode_node const ret_ent = m.message.dict_find_dict("r");
	if (!ret_ent)
	{
		o->timeout();
		return false;
	}

	bdecode_node const node_id_ent = ret_ent.dict_find_string("id");
	if (!node_id_ent || node_id_ent.string_length() != 20)
	{
		o->timeout();
		return false;
	}

	node_id const nid = node_id(node_id_ent.string_ptr());
	if (m_settings.enforce_node_id && !verify_id(nid, m.addr.address()))
	{
		o->timeout();
		return false;
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (m_log->should_log(dht_logger::rpc_manager))
	{
		m_log->log(dht_logger::rpc_manager, "[%u] reply with transaction id: %d from %s"
			, o->algorithm()->id(), int(transaction_id.size())
			, print_endpoint(m.addr).c_str());
	}
#endif
	o->reply(m);
	*id = nid;

	int rtt = int(total_milliseconds(now - o->sent()));

	// we found an observer for this reply, hence the node is not spoofing
	// add it to the routing table
	return m_table.node_seen(*id, m.addr, rtt);
}

time_duration rpc_manager::tick()
{
	INVARIANT_CHECK;

	constexpr int short_timeout = 1;
	constexpr int timeout = 15;

	// look for observers that have timed out

	if (m_transactions.empty()) return seconds(short_timeout);

	std::vector<observer_ptr> timeouts;
	std::vector<observer_ptr> short_timeouts;

	time_duration ret = seconds(short_timeout);
	time_point now = aux::time_now();

	for (auto i = m_transactions.begin(); i != m_transactions.end();)
	{
		observer_ptr o = i->second;

		time_duration diff = now - o->sent();
		if (diff >= seconds(timeout))
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_log->should_log(dht_logger::rpc_manager))
			{
				m_log->log(dht_logger::rpc_manager, "[%u] timing out transaction id: %d from: %s"
					, o->algorithm()->id(), i->first
					, print_endpoint(o->target_ep()).c_str());
			}
#endif
			i = m_transactions.erase(i);
			timeouts.push_back(o);
			continue;
		}

		// don't call short_timeout() again if we've
		// already called it once
		if (diff >= seconds(short_timeout) && !o->has_short_timeout())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_log->should_log(dht_logger::rpc_manager))
			{
				m_log->log(dht_logger::rpc_manager, "[%u] short-timing out transaction id: %d from: %s"
					, o->algorithm()->id(), i->first
					, print_endpoint(o->target_ep()).c_str());
			}
#endif
			++i;

			short_timeouts.push_back(o);
			continue;
		}

		ret = std::min(seconds(timeout) - diff, ret);
		++i;
	}

	std::for_each(timeouts.begin(), timeouts.end(), std::bind(&observer::timeout, _1));
	std::for_each(short_timeouts.begin(), short_timeouts.end(), std::bind(&observer::short_timeout, _1));

	return std::max(ret, duration_cast<time_duration>(milliseconds(200)));
}

void rpc_manager::add_our_id(entry& e)
{
	e["id"] = m_our_id.to_string();
}

bool rpc_manager::invoke(entry& e, udp::endpoint const& target_addr
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
	std::uint16_t const tid = std::uint16_t(random(0x7fff));
	detail::write_uint16(tid, out);
	e["t"] = transaction_id;

	// When a DHT node enters the read-only state, in each outgoing query message,
	// places a 'ro' key in the top-level message dictionary and sets its value to 1.
	if (m_settings.read_only) e["ro"] = 1;

	node& n = o->algorithm()->get_node();
	if (!n.native_address(o->target_addr()))
	{
		a["want"].list().emplace_back(n.protocol_family_name());
	}

	o->set_target(target_addr);

#ifndef TORRENT_DISABLE_LOGGING
	if (m_log != nullptr && m_log->should_log(dht_logger::rpc_manager))
	{
		m_log->log(dht_logger::rpc_manager, "[%u] invoking %s -> %s"
			, o->algorithm()->id(), e["q"].string().c_str()
			, print_endpoint(target_addr).c_str());
	}
#endif

	if (m_sock_man->send_packet(m_sock, e, target_addr))
	{
		m_transactions.insert(std::make_pair(tid, o));
#if TORRENT_USE_ASSERTS
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
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_in_use);
	m_in_use = false;
#endif
}

} } // namespace libtorrent::dht
