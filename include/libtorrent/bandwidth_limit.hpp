/*

Copyright (c) 2007-2018, Arvid Norberg
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

#ifndef TORRENT_BANDWIDTH_CHANNEL_HPP_INCLUDED
#define TORRENT_BANDWIDTH_CHANNEL_HPP_INCLUDED

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/integer_traits.hpp>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/assert.hpp"

namespace libtorrent {

// member of peer_connection
struct TORRENT_EXTRA_EXPORT bandwidth_channel
{
	static boost::int32_t const inf = boost::integer_traits<boost::int32_t>::const_max;

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
	boost::int64_t m_quota_left;

	// the limit is the number of bytes
	// per second we are allowed to use.
	boost::int32_t m_limit;
};

}

#endif
