/*

Copyright (c) 2007, 2009-2010, 2012, 2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BANDWIDTH_CHANNEL_HPP_INCLUDED
#define TORRENT_BANDWIDTH_CHANNEL_HPP_INCLUDED

#include <cstdint>
#include <limits>

#include "libtorrent/assert.hpp"

namespace lt::aux {

// member of peer_connection
struct TORRENT_EXTRA_EXPORT bandwidth_channel
{
	static constexpr int inf = (std::numeric_limits<std::int32_t>::max)();

	bandwidth_channel();

	// 0 means infinite
	void throttle(int limit);
	int throttle() const
	{
		TORRENT_ASSERT_VAL(m_limit >= 0, m_limit);
		TORRENT_ASSERT_VAL(m_limit < inf, m_limit);
		return m_limit;
	}

	int quota_left() const;
	void update_quota(int dt_milliseconds);

	// this is used when connections disconnect with
	// some quota left. It's returned to its bandwidth
	// channels.
	void return_quota(int amount);
	void use_quota(int amount);

	// this is an optimization. If there is more than one second
	// of quota built up in this channel, just apply it right away
	// instead of introducing a delay to split it up evenly. This
	// should especially help in situations where a single peer
	// has a capacity under the rate limit, but would otherwise be
	// held back by the latency of getting bandwidth from the limiter
	bool need_queueing(int amount)
	{
		if (m_quota_left - amount < m_limit) return true;
		m_quota_left -= amount;
		return false;
	}

	// used as temporary storage while distributing
	// bandwidth
	int tmp;

	// this is the number of bytes to distribute this round
	int distribute_quota;

private:

	// this is the amount of bandwidth we have
	// been assigned without using yet.
	std::int64_t m_quota_left;

	// the limit is the number of bytes
	// per second we are allowed to use.
	std::int32_t m_limit;
};

}

#endif
