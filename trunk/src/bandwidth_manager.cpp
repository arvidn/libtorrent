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

#include "libtorrent/invariant_check.hpp"
#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/peer_connection.hpp"
#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
#include "libtorrent/aux_/session_impl.hpp"
#endif

namespace libtorrent
{
	namespace
	{
		const pt::time_duration window_size = pt::seconds(1);
		const int bandwidth_block_size = 17000;
	}

	history_entry::history_entry(intrusive_ptr<peer_connection> p
		, weak_ptr<torrent> t, int a, pt::ptime exp)
		: expires_at(exp), amount(a), peer(p), tor(t)
	{}

	bandwidth_manager::bandwidth_manager(io_service& ios, int channel)
		: m_ios(ios)
		, m_history_timer(m_ios)
		, m_limit(bandwidth_limit::inf)
		, m_current_quota(0)
		, m_channel(channel)
	{}

	void bandwidth_manager::request_bandwidth(intrusive_ptr<peer_connection> peer)
	{
		INVARIANT_CHECK;
		// make sure this peer isn't already in line
		// waiting for bandwidth
#ifndef NDEBUG
		for (std::deque<intrusive_ptr<peer_connection> >::iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			assert(*i < peer || peer < *i);
		}
#endif

		assert(peer->max_assignable_bandwidth(m_channel) > 0);

		m_queue.push_back(peer);
		if (m_queue.size() == 1) hand_out_bandwidth();
	}


#ifndef NDEBUG
	void bandwidth_manager::check_invariant() const
	{
		int current_quota = 0;
		for (std::deque<history_entry>::const_iterator i
			= m_history.begin(), end(m_history.end()); i != end; ++i)
		{
			current_quota += i->amount;
		}

		assert(current_quota == m_current_quota);
	}
#endif

	void bandwidth_manager::add_history_entry(history_entry const& e) try
	{
		INVARIANT_CHECK;
#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
		(*m_ses->m_logger) << "bw history [" << m_channel << "]\n";
#endif

		m_history.push_front(e);
		m_current_quota += e.amount;
		// in case the size > 1 there is already a timer
		// active that will be invoked, no need to set one up
		if (m_history.size() > 1) return;

		m_history_timer.expires_at(e.expires_at);
		m_history_timer.async_wait(bind(&bandwidth_manager::on_history_expire, this, _1));
	}
	catch (std::exception&) { assert(false); }

	void bandwidth_manager::on_history_expire(asio::error_code const& e) try
	{
		INVARIANT_CHECK;

		if (e) return;

#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
		(*m_ses->m_logger) << "bw expire [" << m_channel << "]\n";
#endif

		assert(!m_history.empty());

		pt::ptime now(pt::microsec_clock::universal_time());
		while (!m_history.empty() && m_history.back().expires_at <= now)
		{
			history_entry e = m_history.back();
			m_history.pop_back();
			m_current_quota -= e.amount;
			assert(m_current_quota >= 0);
			intrusive_ptr<peer_connection> c = e.peer;
			shared_ptr<torrent> t = e.tor.lock();
			if (!c->is_disconnecting()) c->expire_bandwidth(m_channel, e.amount);
			if (t) t->expire_bandwidth(m_channel, e.amount);
		}
		
		// now, wait for the next chunk to expire
		if (!m_history.empty())
		{
			m_history_timer.expires_at(m_history.back().expires_at);
			m_history_timer.async_wait(bind(&bandwidth_manager::on_history_expire, this, _1));
		}

		// since some bandwidth just expired, it
		// means we can hand out more (in case there
		// are still consumers in line)
		if (!m_queue.empty()) hand_out_bandwidth();
	}
	catch (std::exception&)
	{
		assert(false);
	};

	void bandwidth_manager::hand_out_bandwidth() try
	{
		INVARIANT_CHECK;
#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
		(*m_ses->m_logger) << "hand out bw [" << m_channel << "]\n";
#endif

		pt::ptime now(pt::microsec_clock::universal_time());

		// available bandwidth to hand out
		int amount = m_limit - m_current_quota;

		while (!m_queue.empty() && amount > 0)
		{
			assert(amount == m_limit - m_current_quota);
			intrusive_ptr<peer_connection> peer = m_queue.front();
			m_queue.pop_front();

			shared_ptr<torrent> t = peer->associated_torrent().lock();
			if (!t) continue;
			if (peer->is_disconnecting())
			{
				t->expire_bandwidth(m_channel, -1);
				continue;
			}

			// at this point, max_assignable may actually be zero. Since
			// the bandwidth quota is subtracted once the data has been
			// send. If the peer was added to the queue while the data was
			// still being sent, max_assignable may have been > 0 at that time.
			int max_assignable = peer->max_assignable_bandwidth(m_channel);

			// so, hand out max_assignable, but no more than
			// the available bandwidth (amount) and no more
			// than the bandwidth_block_size
			int single_amount = std::min(amount
				, std::min(bandwidth_block_size
					, max_assignable));
			amount -= single_amount;
			if (single_amount > 0) peer->assign_bandwidth(m_channel, single_amount);
			t->assign_bandwidth(m_channel, single_amount);
			add_history_entry(history_entry(peer, t, single_amount, now + window_size));
		}
	}
	catch (std::exception& e)
	{ assert(false); };

}
