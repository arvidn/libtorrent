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

#ifndef TORRENT_BANDWIDTH_LIMIT_HPP_INCLUDED
#define TORRENT_BANDWIDTH_LIMIT_HPP_INCLUDED

#include <boost/integer_traits.hpp>

#include "libtorrent/assert.hpp"

namespace libtorrent {

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
		TORRENT_ASSERT(limit > 0);
		m_local_limit = limit;
	}
	
	int throttle() const
	{
		return m_local_limit;
	}

	void assign(int amount)
	{
		TORRENT_ASSERT(amount >= 0);
		m_current_rate += amount;
		m_quota_left += amount;
	}

	void use_quota(int amount)
	{
		TORRENT_ASSERT(amount <= m_quota_left);
		m_quota_left -= amount;
	}

	int quota_left() const
	{
		return (std::max)(m_quota_left, 0);
	}

	void expire(int amount)
	{
		TORRENT_ASSERT(amount >= 0);
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

}

#endif

