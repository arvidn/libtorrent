/*

Copyright (c) 2015-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/string_util.hpp" // for is_space
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/aux_/session_settings.hpp"

namespace libtorrent {

	namespace {
		// wait at least 5 seconds before retrying a failed tracker
		seconds32 constexpr tracker_retry_delay_min{5};

		// never wait more than 60 minutes to retry a tracker
		minutes32 constexpr tracker_retry_delay_max{60};
	}

	announce_entry::announce_entry(string_view u)
		: url(u.to_string())
		, fails(0)
		, updating(false)
		, source(0)
		, verified(false)
		, start_sent(false)
		, complete_sent(false)
		, triggered_manually(false)
	{}

	announce_entry::announce_entry()
		: fails(0)
		, updating(false)
		, source(0)
		, verified(false)
		, start_sent(false)
		, complete_sent(false)
		, triggered_manually(false)
	{}

	announce_entry::~announce_entry() = default;
	announce_entry::announce_entry(announce_entry const&) = default;
	announce_entry& announce_entry::operator=(announce_entry const&) = default;

#ifndef TORRENT_NO_DEPRECATE
	int announce_entry::next_announce_in() const
	{ return int(total_seconds(next_announce - aux::time_now())); }

	int announce_entry::min_announce_in() const
	{ return int(total_seconds(min_announce - aux::time_now())); }
#endif

	void announce_entry::reset()
	{
		start_sent = false;
		next_announce = time_point32::min();
		min_announce = time_point32::min();
	}

	void announce_entry::failed(int const backoff_ratio, seconds32 const retry_interval)
	{
		++fails;
		// the exponential back-off ends up being:
		// 7, 15, 27, 45, 95, 127, 165, ... seconds
		// with the default tracker_backoff of 250
		int const fail_square = int(fails) * int(fails);
		seconds32 const delay = std::max(retry_interval
			, std::min(duration_cast<seconds32>(tracker_retry_delay_max)
				, tracker_retry_delay_min
					+ fail_square * tracker_retry_delay_min * backoff_ratio / 100
			));
		next_announce = aux::time_now32() + delay;
		updating = false;
	}

	bool announce_entry::can_announce(time_point now, bool is_seed) const
	{
		// if we're a seed and we haven't sent a completed
		// event, we need to let this announce through
		bool const need_send_complete = is_seed && !complete_sent;

		return now >= next_announce
			&& (now >= min_announce || need_send_complete)
			&& (fails < fail_limit || fail_limit == 0)
			&& !updating;
	}

	void announce_entry::trim()
	{
		while (!url.empty() && is_space(url[0]))
			url.erase(url.begin());
	}

}
