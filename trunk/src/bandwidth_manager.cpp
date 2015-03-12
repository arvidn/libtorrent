/*

Copyright (c) 2009-2014, Arvid Norberg
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

#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{

	bandwidth_manager::bandwidth_manager(int channel
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		, bool log
#endif		
		)
		: m_queued_bytes(0)
		, m_channel(channel)
		, m_abort(false)
	{
#ifdef TORRENT_VERBOSE_BANDWIDTH_LIMIT
		if (log)
			m_log.open("bandwidth_limiter.log", std::ios::trunc);
		m_start = aux::time_now();
#endif
	}

	void bandwidth_manager::close()
	{
		m_abort = true;

		queue_t tm;
		tm.swap(m_queue);
		m_queued_bytes = 0;

		while (!tm.empty())
		{
			bw_request& bwr = tm.back();
			bwr.peer->assign_bandwidth(m_channel, bwr.assigned);
			tm.pop_back();
		}
	}

#if TORRENT_USE_ASSERTS
	bool bandwidth_manager::is_queued(bandwidth_socket const* peer) const
	{
		for (queue_t::const_iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			if (i->peer.get() == peer) return true;
		}
		return false;
	}
#endif

	int bandwidth_manager::queue_size() const
	{
		return m_queue.size();
	}

	boost::int64_t bandwidth_manager::queued_bytes() const
	{
		return m_queued_bytes;
	}
	
	// non prioritized means that, if there's a line for bandwidth,
	// others will cut in front of the non-prioritized peers.
	// this is used by web seeds
	int bandwidth_manager::request_bandwidth(boost::shared_ptr<bandwidth_socket> const& peer
		, int blk, int priority, bandwidth_channel** chan, int num_channels)
	{
		INVARIANT_CHECK;
		if (m_abort) return 0;

		TORRENT_ASSERT(blk > 0);
		TORRENT_ASSERT(priority > 0);

		// if this assert is hit, the peer is requesting more bandwidth before
		// being assigned bandwidth for an already outstanding request
		TORRENT_ASSERT(!is_queued(peer.get()));

		if (num_channels == 0)
		{
			// the connection is not rate limited by any of its
			// bandwidth channels, or it doesn't belong to any
			// channels. There's no point in adding it to
			// the queue, just satisfy the request immediately
			return blk;
		}

		int k = 0;
		bw_request bwr(peer, blk, priority);
		for (int i = 0; i < num_channels; ++i)
		{
			if (chan[i]->need_queueing(blk))
				bwr.channel[k++] = chan[i];
		}

		if (k == 0) return blk;

		m_queued_bytes += blk;
		m_queue.push_back(bwr);
		return 0;
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void bandwidth_manager::check_invariant() const
	{
		boost::int64_t queued = 0;
		for (queue_t::const_iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			queued += i->request_size - i->assigned;
		}
		TORRENT_ASSERT(queued == m_queued_bytes);
	}
#endif

	void bandwidth_manager::update_quotas(time_duration const& dt)
	{
		if (m_abort) return;
		if (m_queue.empty()) return;

		INVARIANT_CHECK;

		boost::int64_t dt_milliseconds = total_milliseconds(dt);
		if (dt_milliseconds > 3000) dt_milliseconds = 3000;

		// for each bandwidth channel, call update_quota(dt)

		std::vector<bandwidth_channel*> channels;

		queue_t tm;

		for (queue_t::iterator i = m_queue.begin();
			i != m_queue.end();)
		{
			if (i->peer->is_disconnecting())
			{
				m_queued_bytes -= i->request_size - i->assigned;

				// return all assigned quota to all the
				// bandwidth channels this peer belongs to
				for (int j = 0; j < bw_request::max_bandwidth_channels && i->channel[j]; ++j)
				{
					bandwidth_channel* bwc = i->channel[j];
					bwc->return_quota(i->assigned);
				}

				i->assigned = 0;
				tm.push_back(*i);
				i = m_queue.erase(i);
				continue;
			}
			for (int j = 0; j < bw_request::max_bandwidth_channels && i->channel[j]; ++j)
			{
				bandwidth_channel* bwc = i->channel[j];
				bwc->tmp = 0;
			}
			++i;
		}

		for (queue_t::iterator i = m_queue.begin()
			, end(m_queue.end()); i != end; ++i)
		{
			for (int j = 0; j < bw_request::max_bandwidth_channels && i->channel[j]; ++j)
			{
				bandwidth_channel* bwc = i->channel[j];
				if (bwc->tmp == 0) channels.push_back(bwc);
				TORRENT_ASSERT(INT_MAX - bwc->tmp > i->priority);
				bwc->tmp += i->priority;
			}
		}

		for (std::vector<bandwidth_channel*>::iterator i = channels.begin()
			, end(channels.end()); i != end; ++i)
		{
			(*i)->update_quota(int(dt_milliseconds));
		}

		for (queue_t::iterator i = m_queue.begin();
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
			bw_request& bwr = tm.back();
			bwr.peer->assign_bandwidth(m_channel, bwr.assigned);
			tm.pop_back();
		}
	}
}

