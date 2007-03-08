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
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/integer_traits.hpp>
#include <boost/thread/mutex.hpp>
#include <deque>

namespace pt = boost::posix_time;
using boost::weak_ptr;
using boost::shared_ptr;
using boost::intrusive_ptr;
using boost::bind;

namespace libtorrent
{

	class peer_connection;
	class torrent;
	
	// the maximum block of bandwidth quota to
	// hand out is 33kB. The block size may
	// be smaller on lower limits
	const int max_bandwidth_block_size = 33000;
	const int min_bandwidth_block_size = 4000;

#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
	namespace aux
	{
		struct session_impl;
	}
#endif

struct history_entry
{
	history_entry(intrusive_ptr<peer_connection> p, weak_ptr<torrent> t
		, int a, pt::ptime exp);
	pt::ptime expires_at;
	int amount;
	intrusive_ptr<peer_connection> peer;
	weak_ptr<torrent> tor;
};

struct bw_queue_entry
{
	bw_queue_entry(boost::intrusive_ptr<peer_connection> const& pe, bool no_prio);
	boost::intrusive_ptr<peer_connection> peer;
	bool non_prioritized;
};

// member of peer_connection
struct bandwidth_limit
{
	static const int inf = boost::integer_traits<int>::const_max;

	bandwidth_limit()
		: m_quota_left(0)
		, m_local_limit(inf)
		, m_current_rate(0)
	{}

	void throttle(int limit)
	{
		m_local_limit = limit;
	}
	
	int throttle() const
	{
		return m_local_limit;
	}

	void assign(int amount)
	{
		assert(amount > 0);
		m_current_rate += amount;
		m_quota_left += amount;
	}

	void use_quota(int amount)
	{
		assert(amount <= m_quota_left);
		m_quota_left -= amount;
	}

	int quota_left() const
	{
		return (std::max)(m_quota_left, 0);
	}

	void expire(int amount)
	{
		assert(amount >= 0);
		m_current_rate -= amount;
	}

	int max_assignable() const
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

struct bandwidth_manager
{
	bandwidth_manager(io_service& ios, int channel);

	void throttle(int limit)
	{
		mutex_t::scoped_lock l(m_mutex);
		assert(limit >= 0);
		m_limit = limit;
	}
	
	int throttle() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_limit;
	}

	// non prioritized means that, if there's a line for bandwidth,
	// others will cut in front of the non-prioritized peers.
	// this is used by web seeds
	void request_bandwidth(intrusive_ptr<peer_connection> peer
		, bool non_prioritized);

#ifndef NDEBUG
	void check_invariant() const;
#endif
#if defined TORRENT_LOGGING || defined TORRENT_VERBOSE_LOGGING
	aux::session_impl* m_ses;
#endif

private:

	void add_history_entry(history_entry const& e);
	void on_history_expire(asio::error_code const& e);
	void hand_out_bandwidth();

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
	std::deque<bw_queue_entry> m_queue;

	// these are the consumers that have received bandwidth
	// that will expire
	std::deque<history_entry> m_history;

	// this is the channel within the consumers
	// that bandwidth is assigned to (upload or download)
	int m_channel;
};

}

#endif
