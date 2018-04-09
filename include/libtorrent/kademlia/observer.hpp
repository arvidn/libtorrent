/*

Copyright (c) 2007-2018, Arvid Norberg
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

#ifndef OBSERVER_HPP
#define OBSERVER_HPP

#include <libtorrent/time.hpp>
#include <libtorrent/address.hpp>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/pool/pool.hpp>
#include <boost/detail/atomic_count.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {
namespace dht {

struct dht_observer;
struct observer;
struct msg;
struct traversal_algorithm;

// defined in rpc_manager.cpp
TORRENT_EXTRA_EXPORT void intrusive_ptr_add_ref(observer const*);
TORRENT_EXTRA_EXPORT void intrusive_ptr_release(observer const*);

struct TORRENT_EXTRA_EXPORT observer : boost::noncopyable
{
	friend TORRENT_EXTRA_EXPORT void intrusive_ptr_add_ref(observer const*);
	friend TORRENT_EXTRA_EXPORT void intrusive_ptr_release(observer const*);

	observer(boost::intrusive_ptr<traversal_algorithm> const& a
		, udp::endpoint const& ep, node_id const& id)
		: m_sent()
		, m_algorithm(a)
		, m_id(id)
		, m_refs(0)
		, m_port(0)
		, m_transaction_id()
		, flags(0)
	{
		TORRENT_ASSERT(a);
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
		m_in_constructor = true;
		m_was_sent = false;
		m_was_abandoned = false;
		m_in_use = true;
#endif
		set_target(ep);
	}

	// defined in rpc_manager.cpp
	virtual ~observer();

	// this is called when a reply is received
	virtual void reply(msg const& m) = 0;

	// this is called if no response has been received after
	// a few seconds, before the request has timed out
	void short_timeout();

	bool has_short_timeout() const { return (flags & flag_short_timeout) != 0; }

	// this is called when no reply has been received within
	// some timeout, or a reply with incorrect format.
	virtual void timeout();

	// if this is called the destructor should
	// not invoke any new messages, and should
	// only clean up. It means the rpc-manager
	// is being destructed
	void abort();

	dht_observer* get_observer() const;

	traversal_algorithm* algorithm() const { return m_algorithm.get(); }

	time_point sent() const { return m_sent; }

	void set_target(udp::endpoint const& ep);
	address target_addr() const;
	udp::endpoint target_ep() const;

	void set_id(node_id const& id);
	node_id const& id() const { return m_id; }

	void set_transaction_id(boost::uint16_t tid)
	{ m_transaction_id = tid; }

	boost::uint16_t transaction_id() const
	{ return m_transaction_id; }

	enum {
		flag_queried = 1,
		flag_initial = 2,
		flag_no_id = 4,
		flag_short_timeout = 8,
		flag_failed = 16,
		flag_ipv6_address = 32,
		flag_alive = 64,
		flag_done = 128
	};

protected:

	void done();

private:

	time_point m_sent;

	const boost::intrusive_ptr<traversal_algorithm> m_algorithm;

	node_id m_id;

	TORRENT_UNION addr_t
	{
#if TORRENT_USE_IPV6
		address_v6::bytes_type v6;
#endif
		address_v4::bytes_type v4;
	} m_addr;

	// reference counter for intrusive_ptr
	mutable boost::uint16_t m_refs;

	boost::uint16_t m_port;

	// the transaction ID for this call
	boost::uint16_t m_transaction_id;
public:
	unsigned char flags;

#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
	bool m_in_constructor:1;
	bool m_was_sent:1;
	bool m_was_abandoned:1;
	bool m_in_use:1;
#endif
};

typedef boost::intrusive_ptr<observer> observer_ptr;

} }

#endif

