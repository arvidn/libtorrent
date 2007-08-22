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

#include "libtorrent/socket.hpp"
#include "libtorrent/invariant_check.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/integer_traits.hpp>
#include <boost/thread/mutex.hpp>
#include <deque>

using boost::weak_ptr;
using boost::shared_ptr;
using boost::intrusive_ptr;
using boost::bind;

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

template<class PeerConnection>
struct bw_queue_entry
{
	bw_queue_entry(boost::intrusive_ptr<PeerConnection> const& pe
		, int blk, bool no_prio)
		: peer(pe), max_block_size(blk), non_prioritized(no_prio) {}
	boost::intrusive_ptr<PeerConnection> peer;
	int max_block_size;
	bool non_prioritized;
};

// member of peer_connection
struct bandwidth_limit
{
	static const int inf = boost::integer_traits<int>::const_max;

	bandwidth_limit() throw()
		: m_quota_left(0)
		, m_local_limit(inf)
		, m_current_rate(0)
	{}

	void throttle(int limit) throw()
	{
		m_local_limit = limit;
	}
	
	int throttle() const throw()
	{
		return m_local_limit;
	}

	void assign(int amount) throw()
	{
		assert(amount > 0);
		m_current_rate += amount;
		m_quota_left += amount;
	}

	void use_quota(int amount) throw()
	{
		assert(amount <= m_quota_left);
		m_quota_left -= amount;
	}

	int quota_left() const throw()
	{
		return (std::max)(m_quota_left, 0);
	}

	void expire(int amount) throw()
	{
		assert(amount >= 0);
		m_current_rate -= amount;
	}

	int max_assignable() const throw()
	{
		if (m_local_limit == inf) return inf;
		if (m_local_limit <= m_current_rate) return 0;
		return m_local_limit - m_current_rate;
	}

private:

	// this is the amount of bandwidth we have
	// been assigned without using yet. i.e.
	// the bandwidth that we use up every time
	// we receive or send a message. Once this
	// hits zero, we need to request more
	// bandwidth from the torrent which
	// in turn will request bandwidth from
	// the bandwidth manager
	int m_quota_left;

	// the local limit is the number of bytes
	// per window size we are allowed to use.
	int m_local_limit;

	// the current rate is the number of
	// bytes we have been assigned within
	// the window size.
	int m_current_rate;
};

template<class T>
T clamp(T val, T ceiling, T floor) throw()
{
	assert(ceiling >= floor);
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
	{}

	void throttle(int limit) throw()
	{
		mutex_t::scoped_lock l(m_mutex);
		assert(limit >= 0);
		m_limit = limit;
	}
	
	int throttle() const throw()
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_limit;
	}

	// non prioritized means that, if there's a line for bandwidth,
	// others will cut in front of the non-prioritized peers.
	// this is used by web seeds
	void request_bandwidth(intrusive_ptr<PeerConnection> peer
		, int blk
		, bool non_prioritized) throw()
	{
		INVARIANT_CHECK;
		assert(blk > 0);

		assert(!peer->ignore_bandwidth_limits());

		// make sure this peer isn't already in line
		// waiting for bandwidth
#ifndef NDEBUG
		for (typename queue_t::iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			assert(i->peer < peer || peer < i->peer);
		}
#endif

		assert(peer->max_assignable_bandwidth(m_channel) > 0);
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

		if (m_queue.size() == 1) hand_out_bandwidth();
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

		assert(current_quota == m_current_quota);
	}
#endif

private:

	void add_history_entry(history_entry<PeerConnection, Torrent> const& e) throw()
	{
#ifndef NDEBUG
		try {
#endif
		INVARIANT_CHECK;
		m_history.push_front(e);
		m_current_quota += e.amount;
		// in case the size > 1 there is already a timer
		// active that will be invoked, no need to set one up
		if (m_history.size() > 1) return;

		m_history_timer.expires_at(e.expires_at);
		m_history_timer.async_wait(bind(&bandwidth_manager::on_history_expire, this, _1));
#ifndef NDEBUG
		}
		catch (std::exception&) { assert(false); }
#endif
	}
	
	void on_history_expire(asio::error_code const& e) throw()
	{
#ifndef NDEBUG
		try {
#endif
		INVARIANT_CHECK;

		if (e) return;

		assert(!m_history.empty());

		ptime now(time_now());
		while (!m_history.empty() && m_history.back().expires_at <= now)
		{
			history_entry<PeerConnection, Torrent> e = m_history.back();
			m_history.pop_back();
			m_current_quota -= e.amount;
			assert(m_current_quota >= 0);
			intrusive_ptr<PeerConnection> c = e.peer;
			shared_ptr<Torrent> t = e.tor.lock();
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
#ifndef NDEBUG
		}
		catch (std::exception&)
		{
			assert(false);
		}
#endif
	}

	void hand_out_bandwidth() throw()
	{
#ifndef NDEBUG
		try {
#endif
		INVARIANT_CHECK;

		ptime now(time_now());

		mutex_t::scoped_lock l(m_mutex);
		int limit = m_limit;
		l.unlock();

		// available bandwidth to hand out
		int amount = limit - m_current_quota;

		while (!m_queue.empty() && amount > 0)
		{
			assert(amount == limit - m_current_quota);
			bw_queue_entry<PeerConnection> qe = m_queue.front();
			m_queue.pop_front();

			shared_ptr<Torrent> t = qe.peer->associated_torrent().lock();
			if (!t) continue;
			if (qe.peer->is_disconnecting())
			{
				t->expire_bandwidth(m_channel, qe.max_block_size);
				continue;
			}

			// at this point, max_assignable may actually be zero. Since
			// the bandwidth quota is subtracted once the data has been
			// sent. If the peer was added to the queue while the data was
			// still being sent, max_assignable may have been > 0 at that time.
			int max_assignable = (std::min)(
				qe.peer->max_assignable_bandwidth(m_channel)
				, t->max_assignable_bandwidth(m_channel));
			if (max_assignable == 0)
			{
				t->expire_bandwidth(m_channel, qe.max_block_size);
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
			int block_size = (std::min)(qe.max_block_size
				, (std::min)(qe.peer->bandwidth_throttle(m_channel)
				, m_limit / 10));

			if (block_size < min_bandwidth_block_size)
			{
				block_size = min_bandwidth_block_size;
			}
			else if (block_size > max_bandwidth_block_size)
			{
				if (m_limit == bandwidth_limit::inf)
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
					block_size = m_limit
						/ (m_limit / max_bandwidth_block_size);
				}
				if (block_size > qe.max_block_size) block_size = qe.max_block_size;
			}

			if (amount < block_size / 2)
			{
				m_queue.push_front(qe);
				break;
			}

			// so, hand out max_assignable, but no more than
			// the available bandwidth (amount) and no more
			// than the max_bandwidth_block_size
			int hand_out_amount = (std::min)((std::min)(block_size, max_assignable)
				, amount);
			assert(hand_out_amount > 0);
			amount -= hand_out_amount;
			assert(hand_out_amount <= qe.max_block_size);
			t->assign_bandwidth(m_channel, hand_out_amount, qe.max_block_size);
			qe.peer->assign_bandwidth(m_channel, hand_out_amount);
			add_history_entry(history_entry<PeerConnection, Torrent>(
				qe.peer, t, hand_out_amount, now + bw_window_size));
		}
#ifndef NDEBUG
		}
		catch (std::exception& e)
		{ assert(false); };
#endif
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
};

}

#endif

