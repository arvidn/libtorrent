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

#ifndef TORRENT_CONNECTION_QUEUE
#define TORRENT_CONNECTION_QUEUE

#include <list>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include "libtorrent/socket.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{

class connection_queue : public boost::noncopyable
{
public:
	connection_queue(io_service& ios);

	bool free_slots() const;

	void enqueue(boost::function<void(int)> const& on_connect
		, boost::function<void()> const& on_timeout
		, time_duration timeout);
	void done(int ticket);
	void limit(int limit);
	int limit() const;
	void close();

#ifndef NDEBUG

	void check_invariant() const;

#endif

private:

	void try_connect();
	void on_timeout(asio::error_code const& e);

	struct entry
	{
		entry(): connecting(false), ticket(0), expires(max_time()) {}
		// called when the connection is initiated
		boost::function<void(int)> on_connect;
		// called if done hasn't been called within the timeout
		boost::function<void()> on_timeout;
		bool connecting;
		int ticket;
		ptime expires;
		time_duration timeout;
	};

	std::list<entry> m_queue;

	// the next ticket id a connection will be given
	int m_next_ticket;
	int m_num_connecting;
	int m_half_open_limit;

	deadline_timer m_timer;

	typedef boost::recursive_mutex mutex_t;
	mutable mutex_t m_mutex;

#ifndef NDEBUG
	bool m_in_timeout_function;
#endif
};

}

#endif

