/*

Copyright (c) 2009, Arvid Norberg
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

#include "libtorrent/bandwidth_limit.hpp"
#include <algorithm>

namespace libtorrent
{
	bandwidth_channel::bandwidth_channel()
		: tmp(0)
		, distribute_quota(0)
		, m_quota_left(0)
		, m_limit(0)
	{}

	// 0 means infinite
	void bandwidth_channel::throttle(int limit)
	{
		TORRENT_ASSERT(limit >= 0);
		// if the throttle is more than this, we might overflow
		TORRENT_ASSERT(limit < INT_MAX / 31);
		m_limit = limit;
	}
	
	int bandwidth_channel::quota_left() const
	{
		if (m_limit == 0) return inf;
		return (std::max)(int(m_quota_left), 0);
	}

	void bandwidth_channel::update_quota(int dt_milliseconds)
	{
		if (m_limit == 0) return;
		m_quota_left += (m_limit * dt_milliseconds + 500) / 1000;
		if (m_quota_left > m_limit * 3) m_quota_left = m_limit * 3;
		distribute_quota = int((std::max)(m_quota_left, boost::int64_t(0)));
//		fprintf(stderr, "%p: [%d]: + %"PRId64" limit: %"PRId64" quota_left: %"PRId64"\n", this
//			, dt_milliseconds, (m_limit * dt_milliseconds + 500) / 1000, m_limit
//			, m_quota_left);
	}

	// this is used when connections disconnect with
	// some quota left. It's returned to its bandwidth
	// channels.
	void bandwidth_channel::return_quota(int amount)
	{
		TORRENT_ASSERT(amount >= 0);
		if (m_limit == 0) return;
		TORRENT_ASSERT(m_quota_left <= m_quota_left + amount);
		m_quota_left += amount;
	}

	void bandwidth_channel::use_quota(int amount)
	{
		TORRENT_ASSERT(amount >= 0);
		TORRENT_ASSERT(m_limit >= 0);
		if (m_limit == 0) return;

//		fprintf(stderr, "%p: - %"PRId64" limit: %"PRId64" quota_left: %"PRId64"\n", this
//			, amount, m_limit, m_quota_left);
		m_quota_left -= amount;
	}

}

