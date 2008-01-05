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

#ifndef TORRENT_BANDWIDTH_MANAGER_HPP_INCLUDED
#define TORRENT_BANDWIDTH_MANAGER_HPP_INCLUDED

#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/integer_traits.hpp>
#include <boost/thread/mutex.hpp>
#include <deque>

#include "libtorrent/socket.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/bandwidth_queue_entry.hpp"

using boost::weak_ptr;
using boost::shared_ptr;
using boost::intrusive_ptr;
using boost::bind;

//#define TORRENT_VERBOSE_BANDWIDTH_LIMIT

namespace libtorrent {

// the maximum block of bandwidth quota to
// hand out is 33kB. The block size may
// be smaller on lower limits
enum
{
	max_bandwidth_block_size = 33000,
	min_bandwidth_block_size = 400
};

const time_duration bw_window_size = seconds(1);

template<class PeerConnection, class Torrent>
struct history_entry
{
	history_entry(intrusive_ptr<PeerConnection> p, weak_ptr<Torrent> t
		, int a, ptime exp)
		: expires_at(exp), amount(a), peer(p), tor(t) {}
	ptime expires_at;
	int amount;
	intrusive_ptr<PeerConnection> peer;
	weak_ptr<Torrent> tor;
};

template<class T>
T clamp(T val, T ceiling, T floor) throw()
{
	TORRENT_ASSERT(ceiling >= floor);
	if (val >= ceiling) return ceiling;
	else if (val <= floor) return floor;
	return val;
}

template<class PeerConnection, class Torrent>
struct bandwidth_manager
{
	bandwidth_manager(io_service& ios, int channel) throw()
		: m_ios(ios)
		, m_history_timer(m_ios)
		, m_limit(bandwidth_limit::inf)
		, m_current_quota(0)
		, m_channel(channel)
		, m_in_hand_out_bandwidth(false)
		, m_abort(false)
	{}

	void throttle(int limit) throw()
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(limit >= 0);
		m_limit = limit;
	}
	
	int throttle() const throw()
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_limit;
	}

	void close()
	{
		m_abort = true;
		m_history_timer.cancel();
	}

#ifndef NDEBUG
	bool is_queued(PeerConnection const* peer) const
	{
		mutex_t::scoped_lock l(m_mutex);
		for (typename queue_t::const_iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			if (i->peer.get() == peer) return true;
		}
		return false;
	}

	bool is_in_history(PeerConnection const* peer) const
	{
		mutex_t::scoped_lock l(m_mutex);
		for (typename history_t::const_iterator i
			= m_history.begin(), end(m_history.end()); i != end; ++i)
		{
			if (i->peer.get() == peer) return true;
		}
		return false;
	}
#endif

	
	// non prioritized means that, if there's a line for bandwidth,
	// others will cut in front of the non-prioritized peers.
	// this is used by web seeds
	void request_bandwidth(intrusive_ptr<PeerConnection> const& peer
		, int blk
		, bool non_prioritized) throw()
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(blk > 0);
		TORRENT_ASSERT(!is_queued(peer.get()));

		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(!peer->ignore_bandwidth_limits());

		// make sure this peer isn't already in line
		// waiting for bandwidth
		TORRENT_ASSERT(peer->max_assignable_bandwidth(m_channel) > 0);

		boost::shared_ptr<Torrent> t = peer->associated_torrent().lock();
		m_queue.push_back(bw_queue_entry<PeerConnection>(peer, blk, non_prioritized));
		if (!non_prioritized)
		{
			typename queue_t::reverse_iterator i = m_queue.rbegin();
			typename queue_t::reverse_iterator j(i);
			for (++j; j != m_queue.rend(); ++j)
			{
				// if the peer's torrent is not the same one
				// continue looking for a peer from the same torrent
				if (j->peer->associated_torrent().lock() != t)
					continue;
				// if we found a peer from the same torrent that
				// is prioritized, there is no point looking
				// any further.
				if (!j->non_prioritized) break;

				using std::swap;
				swap(*i, *j);
				i = j;
			}
		}
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		std::cerr << " req_bandwidht. m_queue.size() = " << m_queue.size() << std::endl;
#endif
		if (!m_queue.empty()) hand_out_bandwidth(l);
	}

#ifndef NDEBUG
	void check_invariant() const
	{
		int current_quota = 0;
		for (typename history_t::const_iterator i
			= m_history.begin(), end(m_history.end()); i != end; ++i)
		{
			current_quota += i->amount;
		}

		TORRENT_ASSERT(current_quota == m_current_quota);
	}
#endif

private:

	void add_history_entry(history_entry<PeerConnection, Torrent> const& e) throw()
	{
		INVARIANT_CHECK;

		m_history.push_front(e);
		m_current_quota += e.amount;
		// in case the size > 1 there is already a timer
		// active that will be invoked, no need to set one up
		if (m_history.size() > 1) return;

		if (m_abort) return;

		asio::error_code ec;
		m_history_timer.expires_at(e.expires_at, ec);
		m_history_timer.async_wait(bind(&bandwidth_manager::on_history_expire, this, _1));
	}
	
	void on_history_expire(asio::error_code const& e) throw()
	{
		INVARIANT_CHECK;

		if (e) return;

		TORRENT_ASSERT(!m_history.empty());

		mutex_t::scoped_lock l(m_mutex);
		ptime now(time_now());
		while (!m_history.empty() && m_history.back().expires_at <= now)
		{
			history_entry<PeerConnection, Torrent> e = m_history.back();
			m_history.pop_back();
			m_current_quota -= e.amount;
			TORRENT_ASSERT(m_current_quota >= 0);
			intrusive_ptr<PeerConnection> c = e.peer;
			shared_ptr<Torrent> t = e.tor.lock();
			l.unlock();
			if (!c->is_disconnecting()) c->expire_bandwidth(m_channel, e.amount);
			if (t) t->expire_bandwidth(m_channel, e.amount);
			l.lock();
		}
		
		// now, wait for the next chunk to expire
		if (!m_history.empty() && !m_abort)
		{
			asio::error_code ec;
			m_history_timer.expires_at(m_history.back().expires_at, ec);
			m_history_timer.async_wait(bind(&bandwidth_manager::on_history_expire, this, _1));
		}

		// since some bandwidth just expired, it
		// means we can hand out more (in case there
		// are still consumers in line)
		if (!m_queue.empty()) hand_out_bandwidth(l);
	}

	void hand_out_bandwidth(boost::mutex::scoped_lock& l)
	{
		// if we're already handing out bandwidth, just return back
		// to the loop further down on the callstack
		if (m_in_hand_out_bandwidth) return;
		m_in_hand_out_bandwidth = true;

		INVARIANT_CHECK;

		ptime now(time_now());

		int limit = m_limit;

		// available bandwidth to hand out
		int amount = limit - m_current_quota;

#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		std::cerr << " hand_out_bandwidht. m_queue.size() = " << m_queue.size()
			<< " amount = " << amount
			<< " limit = " << limit
			<< " m_current_quota = " << m_current_quota << std::endl;
#endif

		if (amount <= 0)
		{
			m_in_hand_out_bandwidth = false;
			return;
		}

		queue_t q;
		queue_t tmp;
		m_queue.swap(q);
		while (!q.empty() && amount > 0)
		{
			TORRENT_ASSERT(amount == limit - m_current_quota);
			bw_queue_entry<PeerConnection> qe = q.front();
			TORRENT_ASSERT(qe.max_block_size > 0);
			q.pop_front();

			shared_ptr<Torrent> t = qe.peer->associated_torrent().lock();
			if (!t) continue;
			if (qe.peer->is_disconnecting())
			{
				l.unlock();
				t->expire_bandwidth(m_channel, qe.max_block_size);
				l.lock();
				amount = limit - m_current_quota;
				continue;
			}

			// at this point, max_assignable may actually be zero. Since
			// the rate limit of the peer might have changed while it
			// was in the queue.
			int max_assignable = qe.peer->max_assignable_bandwidth(m_channel);
			if (max_assignable == 0)
			{
				TORRENT_ASSERT(is_in_history(qe.peer.get()));
				tmp.push_back(qe);
				continue;
			}

			// this is the limit of the block size. It depends on the throttle
			// so that it can be closer to optimal. Larger block sizes will give lower
			// granularity to the rate but will be more efficient. At high rates
			// the block sizes are bigger and at low rates, the granularity
			// is more important and block sizes are smaller

			// the minimum rate that can be given is the block size, so, the
			// block size must be smaller for lower rates. This is because
			// the history window is one second, and the block will be forgotten
			// after one second.
			int block_size = (std::min)(qe.peer->bandwidth_throttle(m_channel)
				, limit / 10);

			if (block_size < min_bandwidth_block_size)
			{
				block_size = (std::min)(int(min_bandwidth_block_size), limit);
			}
			else if (block_size > max_bandwidth_block_size)
			{
				if (limit == bandwidth_limit::inf)
				{
					block_size = max_bandwidth_block_size;
				}
				else
				{
					// try to make the block_size a divisor of
					// m_limit to make the distributions as fair
					// as possible
					// TODO: move this calculcation to where the limit
					// is changed
					block_size = limit
						/ (limit / max_bandwidth_block_size);
				}
			}
			if (block_size > qe.max_block_size) block_size = qe.max_block_size;

#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
			std::cerr << " block_size = " << block_size << " amount = " << amount << std::endl;
#endif
			if (amount < block_size / 2)
			{
				tmp.push_back(qe);
				break;
			}

			// so, hand out max_assignable, but no more than
			// the available bandwidth (amount) and no more
			// than the max_bandwidth_block_size
			int hand_out_amount = (std::min)((std::min)(block_size, max_assignable)
				, amount);
			TORRENT_ASSERT(hand_out_amount > 0);
			TORRENT_ASSERT(amount == limit - m_current_quota);
			amount -= hand_out_amount;
			TORRENT_ASSERT(hand_out_amount <= qe.max_block_size);
			l.unlock();
			t->assign_bandwidth(m_channel, hand_out_amount, qe.max_block_size);
			qe.peer->assign_bandwidth(m_channel, hand_out_amount);
			l.lock();
			add_history_entry(history_entry<PeerConnection, Torrent>(
				qe.peer, t, hand_out_amount, now + bw_window_size));
			amount = limit - m_current_quota;
		}
		if (!q.empty()) m_queue.insert(m_queue.begin(), q.begin(), q.end());
		if (!tmp.empty()) m_queue.insert(m_queue.begin(), tmp.begin(), tmp.end());
		m_in_hand_out_bandwidth = false;
	}


	typedef boost::mutex mutex_t;
	mutable mutex_t m_mutex;

	// the io_service used for the timer
	io_service& m_ios;

	// the timer that is waiting for the entries
	// in the history queue to expire (slide out
	// of the history window)
	deadline_timer m_history_timer;

	// the rate limit (bytes per second)
	int m_limit;

	// the sum of all recently handed out bandwidth blocks
	int m_current_quota;

	// these are the consumers that want bandwidth
	typedef std::deque<bw_queue_entry<PeerConnection> > queue_t;
	queue_t m_queue;

	// these are the consumers that have received bandwidth
	// that will expire
	typedef std::deque<history_entry<PeerConnection, Torrent> > history_t;
	history_t m_history;

	// this is the channel within the consumers
	// that bandwidth is assigned to (upload or download)
	int m_channel;

	// this is true while we're in the hand_out_bandwidth loop
	// to prevent recursive invocations to interfere
	bool m_in_hand_out_bandwidth;

	bool m_abort;
};

}

#endif

