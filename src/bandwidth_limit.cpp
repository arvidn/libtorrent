/*

Copyright (c) 2009, Georg Rudoy
Copyright (c) 2009-2011, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/bandwidth_limit.hpp"
#include <algorithm>

namespace lt::aux {

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
