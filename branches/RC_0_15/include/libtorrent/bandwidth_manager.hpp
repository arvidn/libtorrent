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

#include <boost/intrusive_ptr.hpp>

#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
#include <fstream>
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/bandwidth_queue_entry.hpp"

using boost::intrusive_ptr;


namespace libtorrent {

template<class PeerConnection>
struct bandwidth_manager
{
	bandwidth_manager(int channel
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		, bool log = false
#endif		
		)
		: m_queued_bytes(0)
		, m_channel(channel)
		, m_abort(false)
	{
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		if (log)
			m_log.open("bandwidth_limiter.log", std::ios::trunc);
		m_start = time_now();
#endif
	}

	void close()
	{
		m_abort = true;
		m_queue.clear();
		m_queued_bytes = 0;
		error_code ec;
	}

#ifdef TORRENT_DEBUG
	bool is_queued(PeerConnection const* peer) const
	{
		for (typename queue_t::const_iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			if (i->peer.get() == peer) return true;
		}
		return false;
	}
#endif

	int queue_size() const
	{
		return int(m_queue.size());
	}

	int queued_bytes() const
	{
		return m_queued_bytes;
	}
	
	// non prioritized means that, if there's a line for bandwidth,
	// others will cut in front of the non-prioritized peers.
	// this is used by web seeds
	void request_bandwidth(intrusive_ptr<PeerConnection> const& peer
		, int blk, int priority
		, bandwidth_channel* chan1 = 0
		, bandwidth_channel* chan2 = 0
		, bandwidth_channel* chan3 = 0
		, bandwidth_channel* chan4 = 0
		, bandwidth_channel* chan5 = 0
		)
	{
		INVARIANT_CHECK;
		if (m_abort) return;

		TORRENT_ASSERT(blk > 0);
		TORRENT_ASSERT(priority > 0);
		TORRENT_ASSERT(!is_queued(peer.get()));

		bw_request<PeerConnection> bwr(peer, blk, priority);
		int i = 0;
		if (chan1 && chan1->throttle() > 0) bwr.channel[i++] = chan1;
		if (chan2 && chan2->throttle() > 0) bwr.channel[i++] = chan2;
		if (chan3 && chan3->throttle() > 0) bwr.channel[i++] = chan3;
		if (chan4 && chan4->throttle() > 0) bwr.channel[i++] = chan4;
		if (chan5 && chan5->throttle() > 0) bwr.channel[i++] = chan5;
		if (i == 0)
		{
			// the connection is not rate limited by any of its
			// bandwidth channels, or it doesn't belong to any
			// channels. There's no point in adding it to
			// the queue, just satisfy the request immediately
			bwr.peer->assign_bandwidth(m_channel, blk);
			return;
		}
		m_queued_bytes += blk;
		m_queue.push_back(bwr);
	}

#ifdef TORRENT_DEBUG
	void check_invariant() const
	{
		int queued = 0;
		for (typename queue_t::const_iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			queued += i->request_size - i->assigned;
		}
		TORRENT_ASSERT(queued == m_queued_bytes);
	}
#endif

	void update_quotas(time_duration const& dt)
	{
		if (m_abort) return;
		if (m_queue.empty()) return;

		INVARIANT_CHECK;

		int dt_milliseconds = total_milliseconds(dt);
		if (dt_milliseconds > 3000) dt_milliseconds = 3000;

		// for each bandwidth channel, call update_quota(dt)

		std::vector<bandwidth_channel*> channels;

		for (typename queue_t::iterator i = m_queue.begin();
			i != m_queue.end();)
		{
			if (i->peer->is_disconnecting())
			{
				m_queued_bytes -= i->request_size - i->assigned;

				// return all assigned quota to all the
				// bandwidth channels this peer belongs to
				for (int j = 0; j < 5 && i->channel[j]; ++j)
				{
					bandwidth_channel* bwc = i->channel[j];
					bwc->return_quota(i->assigned);
				}

				i = m_queue.erase(i);
				continue;
			}
			for (int j = 0; j < 5 && i->channel[j]; ++j)
			{
				bandwidth_channel* bwc = i->channel[j];
				bwc->tmp = 0;
			}
			++i;
		}

		for (typename queue_t::iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			for (int j = 0; j < 5 && i->channel[j]; ++j)
			{
				bandwidth_channel* bwc = i->channel[j];
				if (bwc->tmp == 0) channels.push_back(bwc);
				bwc->tmp += i->priority;
				TORRENT_ASSERT(i->priority > 0);
			}
		}

		for (std::vector<bandwidth_channel*>::iterator i = channels.begin()
			, end(channels.end()); i != end; ++i)
		{
			(*i)->update_quota(dt_milliseconds);
		}

		queue_t tm;

		for (typename queue_t::iterator i = m_queue.begin();
			i != m_queue.end();)
		{
			int a = i->assign_bandwidth();
			if (i->assigned == i->request_size
				|| (i->ttl <= 0 && i->assigned > 0))
			{
				a += i->request_size - i->assigned;
				TORRENT_ASSERT(i->assigned <= i->request_size);
				tm.push_back(*i);
				i = m_queue.erase(i);
			}
			else
			{
				++i;
			}
			m_queued_bytes -= a;
		}

		while (!tm.empty())
		{
			bw_request<PeerConnection>& bwr = tm.back();
			bwr.peer->assign_bandwidth(m_channel, bwr.assigned);
			tm.pop_back();
		}
	}


	// these are the consumers that want bandwidth
	typedef std::vector<bw_request<PeerConnection> > queue_t;
	queue_t m_queue;
	// the number of bytes all the requests in queue are for
	int m_queued_bytes;

	// this is the channel within the consumers
	// that bandwidth is assigned to (upload or download)
	int m_channel;

	bool m_abort;

#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
	std::ofstream m_log;
	ptime m_start;
#endif
};

}

#endif

