/*

Copyright (c) 2007, Arvid Norberg
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

#include <boost/pool/pool.hpp>
#include <boost/detail/atomic_count.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/cstdint.hpp>

namespace libtorrent {
namespace dht {

struct observer;
struct msg;

// defined in rpc_manager.cpp
TORRENT_EXPORT void intrusive_ptr_add_ref(observer const*);
TORRENT_EXPORT void intrusive_ptr_release(observer const*);

struct observer : boost::noncopyable
{
	friend TORRENT_EXPORT void intrusive_ptr_add_ref(observer const*);
	friend TORRENT_EXPORT void intrusive_ptr_release(observer const*);

	observer(boost::pool<>& p)
		: sent(time_now())
		, pool_allocator(p)
		, m_refs(0)
	{
#ifdef TORRENT_DEBUG
		m_in_constructor = true;
#endif
	}

	virtual ~observer()
	{
		TORRENT_ASSERT(!m_in_constructor);
	}

	// these two callbacks lets the observer add
	// information to the message before it's sent
	virtual void send(msg& m) = 0;

	// this is called when a reply is received
	virtual void reply(msg const& m) = 0;

	// this is called when no reply has been received within
	// some timeout
	virtual void timeout() = 0;
	
	// if this is called the destructor should
	// not invoke any new messages, and should
	// only clean up. It means the rpc-manager
	// is being destructed
	virtual void abort() = 0;

#if TORRENT_USE_IPV6
	address target_addr;
#else
	address_v4 target_addr;
#endif
	boost::uint16_t port;
	udp::endpoint target_ep() const { return udp::endpoint(target_addr, port); }
	ptime sent;
#ifdef TORRENT_DEBUG
	bool m_in_constructor;
#endif
private:
	boost::pool<>& pool_allocator;
	// reference counter for intrusive_ptr
	mutable boost::detail::atomic_count m_refs;
};

typedef boost::intrusive_ptr<observer> observer_ptr;

} }

#endif
