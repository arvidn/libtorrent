
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

#include <boost/bind.hpp>
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/connection_queue.hpp"

namespace libtorrent
{

	connection_queue::connection_queue(io_service& ios): m_next_ticket(0)
		, m_num_connecting(0)
		, m_half_open_limit(0)
		, m_timer(ios)
#ifndef NDEBUG
		, m_in_timeout_function(false)
#endif
	{}

	bool connection_queue::free_slots() const
	{ return m_num_connecting < m_half_open_limit || m_half_open_limit <= 0; }

	void connection_queue::enqueue(boost::function<void(int)> const& on_connect
		, boost::function<void()> const& on_timeout
		, time_duration timeout)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		m_queue.push_back(entry());
		entry& e = m_queue.back();
		e.on_connect = on_connect;
		e.on_timeout = on_timeout;
		e.ticket = m_next_ticket;
		e.timeout = timeout;
		++m_next_ticket;
		try_connect();
	}

	void connection_queue::done(int ticket)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;

		std::list<entry>::iterator i = std::find_if(m_queue.begin()
			, m_queue.end(), boost::bind(&entry::ticket, _1) == ticket);
		if (i == m_queue.end())
		{
			// this might not be here in case on_timeout calls remove
			return;
		}
		if (i->connecting) --m_num_connecting;
		m_queue.erase(i);
		try_connect();
	}

	void connection_queue::close()
	{
		m_timer.cancel();
	}

	void connection_queue::limit(int limit)
	{ m_half_open_limit = limit; }

	int connection_queue::limit() const
	{ return m_half_open_limit; }

#ifndef NDEBUG

	void connection_queue::check_invariant() const
	{
		int num_connecting = 0;
		for (std::list<entry>::const_iterator i = m_queue.begin();
			i != m_queue.end(); ++i)
		{
			if (i->connecting) ++num_connecting;
		}
		TORRENT_ASSERT(num_connecting == m_num_connecting);
	}

#endif

	void connection_queue::try_connect()
	{
		INVARIANT_CHECK;

		if (!free_slots())
			return;
	
		if (m_queue.empty())
		{
			m_timer.cancel();
			return;
		}

		std::list<entry>::iterator i = std::find_if(m_queue.begin()
			, m_queue.end(), boost::bind(&entry::connecting, _1) == false);
		while (i != m_queue.end())
		{
			TORRENT_ASSERT(i->connecting == false);
			ptime expire = time_now() + i->timeout;
			if (m_num_connecting == 0)
			{
				m_timer.expires_at(expire);
				m_timer.async_wait(boost::bind(&connection_queue::on_timeout, this, _1));
			}
			i->connecting = true;
			++m_num_connecting;
			i->expires = expire;

			INVARIANT_CHECK;

			entry& ent = *i;
			++i;
			try { ent.on_connect(ent.ticket); } catch (std::exception&) {}

			if (!free_slots()) break;
			i = std::find_if(i, m_queue.end(), boost::bind(&entry::connecting, _1) == false);
		}
	}

#ifndef NDEBUG
	struct function_guard
	{
		function_guard(bool& v): val(v) { TORRENT_ASSERT(!val); val = true; }
		~function_guard() { val = false; }

		bool& val;
	};
#endif
	
	void connection_queue::on_timeout(asio::error_code const& e)
	{
		mutex_t::scoped_lock l(m_mutex);

		INVARIANT_CHECK;
#ifndef NDEBUG
		function_guard guard_(m_in_timeout_function);
#endif

		TORRENT_ASSERT(!e || e == asio::error::operation_aborted);
		if (e) return;

		ptime next_expire = max_time();
		ptime now = time_now();
		std::list<entry> timed_out;
		for (std::list<entry>::iterator i = m_queue.begin();
			!m_queue.empty() && i != m_queue.end();)
		{
			if (i->connecting && i->expires < now)
			{
				std::list<entry>::iterator j = i;
				++i;
				timed_out.splice(timed_out.end(), m_queue, j, i);
				--m_num_connecting;
				continue;
			}
			if (i->expires < next_expire)
				next_expire = i->expires;
			++i;
		}

		// we don't want to call the timeout callback while we're locked
		// since that is a recepie for dead-locks
		l.unlock();

		for (std::list<entry>::iterator i = timed_out.begin()
			, end(timed_out.end()); i != end; ++i)
		{
			try { i->on_timeout(); } catch (std::exception&) {}
		}
		
		l.lock();
		
		if (next_expire < max_time())
		{
			m_timer.expires_at(next_expire);
			m_timer.async_wait(boost::bind(&connection_queue::on_timeout, this, _1));
		}
		try_connect();
	}

}

