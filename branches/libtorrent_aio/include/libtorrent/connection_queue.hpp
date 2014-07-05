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

#ifndef TORRENT_CONNECTION_QUEUE_HPP
#define TORRENT_CONNECTION_QUEUE_HPP

#include <vector>
#include <map>
#include <boost/noncopyable.hpp>
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/deadline_timer.hpp"

#ifdef TORRENT_CONNECTION_LOGGING
#include <fstream>
#endif

#include "libtorrent/thread.hpp"
#include "libtorrent/debug.hpp"

namespace libtorrent
{

struct connection_interface;

class TORRENT_EXTRA_EXPORT connection_queue
	: public boost::noncopyable
	, single_threaded
{
public:
	connection_queue(io_service& ios);

	// if there are no free slots, returns the negative
	// number of queued up connections
	int free_slots() const;

	void enqueue(connection_interface* conn
		, time_duration timeout, int priority = 0);
	bool cancel(connection_interface* conn);
	bool done(int ticket);
	void limit(int limit);
	int limit() const;
	void close();
	int size() const { return m_queue.size(); }
	int num_connecting() const { return int(m_connecting.size()); }
#if defined TORRENT_ASIO_DEBUGGING
	float next_timeout() const { return total_milliseconds(m_timer.expires_at() - time_now_hires()) / 1000.f; }
	float max_timeout() const
	{
		ptime max_timeout = min_time();
		for (std::map<int, connect_entry>::const_iterator i = m_connecting.begin()
			, end(m_connecting.end()); i != end; ++i)
		{
			if (i->second.expires > max_timeout) max_timeout = i->second.expires;
		}
		if (max_timeout == min_time()) return 0.f;
		return total_milliseconds(max_timeout - time_now_hires()) / 1000.f;
	}
#endif

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

private:

	void try_connect();
	void on_timeout(error_code const& e);
	void on_try_connect();

	struct queue_entry
	{
		queue_entry(): conn(0), priority(0) {}
		connection_interface* conn;
		time_duration timeout;
		boost::int32_t ticket;
		bool connecting;
		boost::uint8_t priority;
	};
	struct connect_entry
	{
		connect_entry(): conn(0), expires(max_time()), priority(0) {}
		connection_interface* conn;
		ptime expires;
		int priority;
	};

	std::vector<queue_entry> m_queue;
	std::map<int, connect_entry> m_connecting;

	// the next ticket id a connection will be given
	int m_next_ticket;
	int m_half_open_limit;

	// the number of outstanding timers
	int m_num_timers;

	deadline_timer m_timer;

#ifdef TORRENT_DEBUG
	bool m_in_timeout_function;
#endif
#ifdef TORRENT_CONNECTION_LOGGING
	std::ofstream m_log;
#endif
};

}

#endif

