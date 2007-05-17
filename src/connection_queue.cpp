
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
	{}

	bool connection_queue::free_slots() const
	{ return m_num_connecting < m_half_open_limit || m_half_open_limit <= 0; }

	void connection_queue::enqueue(boost::function<void(int)> const& on_connect
		, boost::function<void()> const& on_timeout
		, time_duration timeout)
	{
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
		assert(num_connecting == m_num_connecting);
	}

#endif

	void connection_queue::try_connect()
	{
		INVARIANT_CHECK;

		if (!free_slots() || m_queue.empty())
			return;

		std::list<entry>::iterator i = std::find_if(m_queue.begin()
			, m_queue.end(), boost::bind(&entry::connecting, _1) == false);
		while (i != m_queue.end())
		{
			assert(i->connecting == false);
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
			try { ent.on_connect(i->ticket); } catch (std::exception&) {}

			if (!free_slots()) break;
			i = std::find_if(i, m_queue.end(), boost::bind(&entry::connecting, _1) == false);
		}
	}

	void connection_queue::on_timeout(asio::error_code const& e)
	{
		INVARIANT_CHECK;

		assert(!e || e == asio::error::operation_aborted);
		if (e) return;

		ptime next_expire = max_time();
		ptime now = time_now();
		for (std::list<entry>::iterator i = m_queue.begin();
			i != m_queue.end();)
		{
			if (i->connecting && i->expires < now)
			{
				boost::function<void()> on_timeout = i->on_timeout;
				m_queue.erase(i++);
				--m_num_connecting;
				try { on_timeout(); } catch (std::exception&) {}
				continue;
			}
			if (i->expires < next_expire)
				next_expire = i->expires;
			++i;
		}
		if (next_expire < max_time())
		{
			m_timer.expires_at(next_expire);
			m_timer.async_wait(boost::bind(&connection_queue::on_timeout, this, _1));
		}
		try_connect();
	}

}

