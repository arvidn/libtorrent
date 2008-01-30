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
	}

	history_entry::history_entry(intrusive_ptr<peer_connection> p
		, weak_ptr<torrent> t, int a, pt::ptime exp)
		: expires_at(exp), amount(a), peer(p), tor(t)
	{}
	
	bw_queue_entry::bw_queue_entry(intrusive_ptr<peer_connection> const& pe
		, bool no_prio)
		: peer(pe), non_prioritized(no_prio)
	{}

	bandwidth_manager::bandwidth_manager(io_service& ios, int channel)
		: m_ios(ios)
		, m_history_timer(m_ios)
		, m_limit(bandwidth_limit::inf)
		, m_current_quota(0)
		, m_channel(channel)
	{}

	void bandwidth_manager::request_bandwidth(intrusive_ptr<peer_connection> peer
		, bool non_prioritized)
	{
		INVARIANT_CHECK;

		// make sure this peer isn't already in line
		// waiting for bandwidth
#ifndef NDEBUG
		for (std::deque<bw_queue_entry>::iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			assert(i->peer < peer || peer < i->peer);
		}
#endif

		assert(peer->max_assignable_bandwidth(m_channel) > 0);
		
		// if the queue is empty, we have to push the new
		// peer at the back of it. If the peer is non-prioritized
		// it is not supposed to cut in fron of anybody, so then
		// we also just add it at the end
		if (m_queue.empty() || non_prioritized)
		{
			m_queue.push_back(bw_queue_entry(peer, non_prioritized));
		}
		else
		{
			// skip forward in the queue until we find a prioritized peer
			// or hit the front of it.
			std::deque<bw_queue_entry>::reverse_iterator i = m_queue.rbegin();
			while (i != m_queue.rend() && i->non_prioritized) ++i;
			m_queue.insert(i.base(), bw_queue_entry(peer, non_prioritized));
		}
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
//		(*m_ses->m_logger) << "bw history [" << m_channel << "]\n";
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
//		(*m_ses->m_logger) << "bw expire [" << m_channel << "]\n";
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
//		(*m_ses->m_logger) << "hand out bw [" << m_channel << "]\n";
#endif

		pt::ptime now(pt::microsec_clock::universal_time());

		mutex_t::scoped_lock l(m_mutex);
		int limit = m_limit;
		l.unlock();

		// available bandwidth to hand out
		int amount = limit - m_current_quota;

		int bandwidth_block_size_limit = max_bandwidth_block_size;
		if (m_queue.size() > 3 && bandwidth_block_size_limit > limit / int(m_queue.size()))
			bandwidth_block_size_limit = std::max(max_bandwidth_block_size / int(m_queue.size() - 3)
				, min_bandwidth_block_size);

		while (!m_queue.empty() && amount > 0)
		{
			assert(amount == limit - m_current_quota);
			bw_queue_entry qe = m_queue.front();
			m_queue.pop_front();

			shared_ptr<torrent> t = qe.peer->associated_torrent().lock();
			if (!t) continue;
			if (qe.peer->is_disconnecting())
			{
				t->expire_bandwidth(m_channel, -1);
				continue;
			}

			// at this point, max_assignable may actually be zero. Since
			// the bandwidth quota is subtracted once the data has been
			// send. If the peer was added to the queue while the data was
			// still being sent, max_assignable may have been > 0 at that time.
			int max_assignable = qe.peer->max_assignable_bandwidth(m_channel);
			if (max_assignable == 0)
			{
				t->expire_bandwidth(m_channel, -1);
				continue;
			}
			// don't hand out chunks larger than the throttle
			// per second on the torrent
			if (max_assignable > t->bandwidth_throttle(m_channel))
				max_assignable = t->bandwidth_throttle(m_channel);

			// so, hand out max_assignable, but no more than
			// the available bandwidth (amount) and no more
			// than the max_bandwidth_block_size
			int single_amount = std::min(amount
				, std::min(bandwidth_block_size_limit
					, max_assignable));
			assert(single_amount > 0);
			amount -= single_amount;
			qe.peer->assign_bandwidth(m_channel, single_amount);
			t->assign_bandwidth(m_channel, single_amount);
			add_history_entry(history_entry(qe.peer, t, single_amount, now + window_size));
		}
	}
	catch (std::exception& e)
	{ assert(false); };

}
