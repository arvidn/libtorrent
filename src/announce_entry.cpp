/*

Copyright (c) 2015-2021, Arvid Norberg
Copyright (c) 2016-2017, 2020, Alden Torres
Copyright (c) 2017-2018, Steven Siloti
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
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/announce_entry.hpp"

namespace libtorrent {

	namespace {
		// wait at least 5 seconds before retrying a failed tracker
		seconds32 constexpr tracker_retry_delay_min{ 5 };

		// never wait more than 60 minutes to retry a tracker
		minutes32 constexpr tracker_retry_delay_max{ 60 };
	}

TORRENT_VERSION_NAMESPACE_2

	announce_infohash::announce_infohash()
		: fails(0)
		, updating(false)
		, start_sent(false)
		, complete_sent(false)
		, triggered_manually(false)
	{}

	announce_entry::announce_entry(string_view u)
		: url(u.to_string())
		, source(0)
		, verified(false)
#if TORRENT_ABI_VERSION == 1
		, fails(0)
		, send_stats(false)
		, start_sent(false)
		, complete_sent(false)
		, triggered_manually(false)
		, updating(false)
#endif
	{}

	announce_entry::announce_entry()
		: source(0)
		, verified(false)
#if TORRENT_ABI_VERSION == 1
		, fails(0)
		, send_stats(false)
		, start_sent(false)
		, complete_sent(false)
		, triggered_manually(false)
		, updating(false)
#endif
	{}

	announce_entry::~announce_entry() = default;
	announce_entry::announce_entry(announce_entry const&) = default;
	announce_entry& announce_entry::operator=(announce_entry const&) & = default;

	announce_endpoint::announce_endpoint() = default;

#if TORRENT_ABI_VERSION <= 2
	void announce_infohash::reset()
	{
		start_sent = false;
		next_announce = time_point32::min();
		min_announce = time_point32::min();
	}

	void announce_infohash::failed(int const backoff_ratio, seconds32 const retry_interval)
	{
		// fails is only 7 bits
		if (fails < (1 << 7) - 1) ++fails;

		// the exponential back-off ends up being:
		// 7, 15, 27, 45, 95, 127, 165, ... seconds
		// with the default tracker_backoff of 250
		int const fail_square = int(fails) * int(fails);
		seconds32 const delay = std::max(retry_interval
			, std::min(duration_cast<seconds32>(tracker_retry_delay_max)
				, tracker_retry_delay_min
					+ fail_square * tracker_retry_delay_min * backoff_ratio / 100
			));
		TORRENT_ASSERT(delay <= tracker_retry_delay_max);
		if (!is_working()) next_announce = aux::time_now32() + delay;
		updating = false;
	}

	bool announce_infohash::can_announce(time_point now, bool is_seed, std::uint8_t fail_limit) const
	{
		TORRENT_ASSERT(next_announce <= now + tracker_retry_delay_max);
		// if we're a seed and we haven't sent a completed
		// event, we need to let this announce through
		bool const need_send_complete = is_seed && !complete_sent;

		// add some slack here for rounding errors
		return now + seconds(1) >= next_announce
			&& (now >= min_announce || need_send_complete)
			&& (fails < fail_limit || fail_limit == 0)
			&& !updating;
	}

	void announce_endpoint::reset()
	{
		for (auto& ih : info_hashes)
			ih.reset();

		start_sent = false;
		next_announce = time_point32::min();
		min_announce = time_point32::min();
	}

	void announce_entry::reset()
	{
		for (auto& aep : endpoints)
			aep.reset();
	}

	bool announce_endpoint::can_announce(time_point now, bool is_seed, std::uint8_t fail_limit) const
	{
		return std::any_of(std::begin(info_hashes), std::end(info_hashes)
			, [&](announce_infohash const& ih) { return ih.can_announce(now, is_seed, fail_limit); });
	}

	bool announce_endpoint::is_working() const
	{
		return std::any_of(std::begin(info_hashes), std::end(info_hashes)
			, [](announce_infohash const& ih) { return ih.is_working(); });
	}

	void announce_entry::trim()
	{
		while (!url.empty() && is_space(url[0]))
			url.erase(url.begin());
	}
#endif
#if TORRENT_ABI_VERSION == 1
	bool announce_entry::can_announce(time_point now, bool is_seed) const
	{
		return std::any_of(endpoints.begin(), endpoints.end()
			, [&](announce_endpoint const& aep) { return aep.can_announce(now, is_seed, fail_limit); });
	}

	bool announce_entry::is_working() const
	{
		return std::any_of(endpoints.begin(), endpoints.end()
			, [](announce_endpoint const& aep) { return aep.is_working(); });
	}
#endif

TORRENT_VERSION_NAMESPACE_2_END

namespace aux {

	announce_infohash::announce_infohash()
		: fails(0)
		, updating(false)
		, start_sent(false)
		, complete_sent(false)
		, triggered_manually(false)
	{}

	announce_endpoint::announce_endpoint(aux::listen_socket_handle const& s, bool const completed)
		: local_endpoint(s ? s.get_local_endpoint() : tcp::endpoint())
		, enabled(true)
		, socket(s)
	{
		TORRENT_UNUSED(completed);
	}

	announce_entry::announce_entry(string_view u)
		: url(u.to_string())
		, source(0)
		, verified(false)
	{}

	announce_entry::announce_entry(lt::announce_entry const& ae)
		: url(ae.url)
		, trackerid(ae.trackerid)
		, tier(ae.tier)
		, fail_limit(ae.fail_limit)
		, source(ae.source)
		, verified(false)
	{
		if (source == 0) source = lt::announce_entry::source_client;
	}

	announce_entry::announce_entry()
		: source(0)
		, verified(false)
	{}

	announce_entry::~announce_entry() = default;
	announce_entry::announce_entry(announce_entry const&) = default;
	announce_entry& announce_entry::operator=(announce_entry const&) & = default;

	void announce_infohash::reset()
	{
		start_sent = false;
		next_announce = time_point32::min();
		min_announce = time_point32::min();
	}

	void announce_infohash::failed(int const backoff_ratio, seconds32 const retry_interval)
	{
		// fails is only 7 bits
		if (fails < (1 << 7) - 1) ++fails;

		// the exponential back-off ends up being:
		// 7, 15, 27, 45, 95, 127, 165, ... seconds
		// with the default tracker_backoff of 250
		int const fail_square = int(fails) * int(fails);
		seconds32 const delay = std::max(retry_interval
			, std::min(duration_cast<seconds32>(tracker_retry_delay_max)
				, tracker_retry_delay_min
					+ fail_square * tracker_retry_delay_min * backoff_ratio / 100
			));
		if (!is_working()) next_announce = aux::time_now32() + delay;
		updating = false;
	}

	bool announce_infohash::can_announce(time_point now, bool is_seed, std::uint8_t fail_limit) const
	{
		// if we're a seed and we haven't sent a completed
		// event, we need to let this announce through
		bool const need_send_complete = is_seed && !complete_sent;

		// add some slack here for rounding errors
		return now  + seconds(1) >= next_announce
			&& (now >= min_announce || need_send_complete)
			&& (fails < fail_limit || fail_limit == 0)
			&& !updating;
	}

	void announce_endpoint::reset()
	{
		for (auto& ih : info_hashes)
			ih.reset();
	}

	void announce_entry::reset()
	{
		for (auto& aep : endpoints)
			aep.reset();
	}

	announce_endpoint* announce_entry::find_endpoint(aux::listen_socket_handle const& s)
	{
		auto aep = std::find_if(endpoints.begin(), endpoints.end()
			, [&](aux::announce_endpoint const& a) { return a.socket == s; });
		if (aep != endpoints.end()) return &*aep;
		else return nullptr;
	}
} // aux
} // libtorrent
