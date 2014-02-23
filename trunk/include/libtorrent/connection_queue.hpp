/*

Copyright (c) 2007-2014, Arvid Norberg
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

#ifndef TORRENT_CONNECTION_QUEUE
#define TORRENT_CONNECTION_QUEUE

#include <list>
#include <boost/function/function1.hpp>
#include <boost/function/function0.hpp>
#include <boost/noncopyable.hpp>
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/deadline_timer.hpp"

#ifdef TORRENT_CONNECTION_LOGGING
#include <fstream>
#endif

#include "libtorrent/thread.hpp"

namespace libtorrent
{

class TORRENT_EXTRA_EXPORT connection_queue : public boost::noncopyable
{
public:
	connection_queue(io_service& ios);

	// if there are no free slots, returns the negative
	// number of queued up connections
	int free_slots() const;

	void enqueue(boost::function<void(int)> const& on_connect
		, boost::function<void()> const& on_timeout
		, time_duration timeout, int priority = 0);
	bool done(int ticket);
	void limit(int limit);
	int limit() const;
	void close();
	int size() const { return m_queue.size(); }
	int num_connecting() const { return m_num_connecting; }
#if defined TORRENT_ASIO_DEBUGGING
	float next_timeout() const { return total_milliseconds(m_timer.expires_at() - time_now_hires()) / 1000.f; }
	float max_timeout() const
	{
		ptime max_timeout = min_time();
		for (std::list<entry>::const_iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			if (!i->connecting) continue;
			if (i->expires > max_timeout) max_timeout = i->expires;
		}
		if (max_timeout == min_time()) return 0.f;
		return total_milliseconds(max_timeout - time_now_hires()) / 1000.f;
	}
#endif

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

private:

	typedef mutex mutex_t;

	void try_connect(mutex_t::scoped_lock& l);
	void on_timeout(error_code const& e);
	void on_try_connect();

	struct entry
	{
		entry()
			: expires(max_time())
			, ticket(0)
			, connecting(false)
			, priority(0)
		{}
		// called when the connection is initiated
		// this is when the timeout countdown starts
		boost::function<void(int)> on_connect;
		// called if done hasn't been called within the timeout
		// or if the connection queue aborts. This means there
		// are 3 different interleaves of these function calls:
		// 1. on_connect
		// 2. on_connect, on_timeout
		// 3. on_timeout
		boost::function<void()> on_timeout;
		ptime expires;
		time_duration timeout;
		boost::uint32_t ticket;
		bool connecting;
		boost::uint8_t priority;
	};

	std::list<entry> m_queue;

	// the next ticket id a connection will be given
	int m_next_ticket;
	int m_num_connecting;
	int m_half_open_limit;
	bool m_abort;

	// the number of outstanding timers
	int m_num_timers;

	deadline_timer m_timer;

	mutable mutex_t m_mutex;

#ifdef TORRENT_DEBUG
	bool m_in_timeout_function;
#endif
#ifdef TORRENT_CONNECTION_LOGGING
	std::ofstream m_log;
#endif
};

}

#endif

