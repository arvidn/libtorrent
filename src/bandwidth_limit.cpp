/*

Copyright (c) 2009-2011, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2009, Georg Rudoy
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2018, 2020, Alden Torres
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

#include "libtorrent/aux_/bandwidth_limit.hpp"
#include <algorithm>

namespace libtorrent {
namespace aux {

	bandwidth_channel::bandwidth_channel()
		: tmp(0)
		, distribute_quota(0)
		, m_quota_left(0)
		, m_limit(0)
	{}

	// 0 means infinite
	void bandwidth_channel::throttle(int const limit)
	{
		TORRENT_ASSERT_VAL(limit >= 0, limit);
		// if the throttle is more than this, we might overflow
		TORRENT_ASSERT_VAL(limit < inf, limit);
		m_limit = limit;
	}

	int bandwidth_channel::quota_left() const
	{
		if (m_limit == 0) return inf;
		return std::max(int(m_quota_left), 0);
	}

	void bandwidth_channel::update_quota(int const dt_milliseconds)
	{
		TORRENT_ASSERT_VAL(m_limit >= 0, m_limit);
		TORRENT_ASSERT_VAL(m_limit < inf, m_limit);

		if (m_limit == 0) return;

		// "to_add" should never have int64 overflow: "m_limit" contains < "<int>::max"
		std::int64_t const to_add = (std::int64_t(m_limit) * dt_milliseconds + 500) / 1000;

		if (to_add > inf - m_quota_left)
		{
			m_quota_left = inf;
		}
		else
		{
			m_quota_left += to_add;
			if (m_quota_left / 3 > m_limit) m_quota_left = std::int64_t(m_limit) * 3;
			// "m_quota_left" will never have int64 overflow but may exceed "<int>::max"
			m_quota_left = std::min(m_quota_left, std::int64_t(inf));
		}

		distribute_quota = int(std::max(m_quota_left, std::int64_t(0)));
	}

	// this is used when connections disconnect with
	// some quota left. It's returned to its bandwidth
	// channels.
	void bandwidth_channel::return_quota(int const amount)
	{
		TORRENT_ASSERT(amount >= 0);
		if (m_limit == 0) return;
		TORRENT_ASSERT(m_quota_left <= m_quota_left + amount);
		m_quota_left += amount;
	}

	void bandwidth_channel::use_quota(int const amount)
	{
		TORRENT_ASSERT(amount >= 0);
		TORRENT_ASSERT(m_limit >= 0);
		if (m_limit == 0) return;

		m_quota_left -= amount;
	}

}
}
