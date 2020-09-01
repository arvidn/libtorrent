/*

Copyright (c) 2015, 2017-2020, Arvid Norberg
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

#ifndef TORRENT_SESSION_STATS_HPP_INCLUDED
#define TORRENT_SESSION_STATS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"

#include <vector>

namespace libtorrent {

	enum class metric_type_t
	{
		counter, gauge
	};

	// describes one statistics metric from the session. For more information,
	// see the session-statistics_ section.
	struct TORRENT_EXPORT stats_metric
	{
		// the name of the counter or gauge
		char const* name;

		// the index into the session stats array, where the underlying value of
		// this counter or gauge is found. The session stats array is part of the
		// session_stats_alert object.
		int value_index;
#if TORRENT_ABI_VERSION == 1
		TORRENT_DEPRECATED static constexpr metric_type_t type_counter = metric_type_t::counter;
		TORRENT_DEPRECATED static constexpr metric_type_t type_gauge = metric_type_t::gauge;
#endif
		metric_type_t type;
	};

	// This free function returns the list of available metrics exposed by
	// libtorrent's statistics API. Each metric has a name and a *value index*.
	// The value index is the index into the array in session_stats_alert where
	// this metric's value can be found when the session stats is sampled (by
	// calling post_session_stats()).
	TORRENT_EXPORT std::vector<stats_metric> session_stats_metrics();

	// given a name of a metric, this function returns the counter index of it,
	// or -1 if it could not be found. The counter index is the index into the
	// values array returned by session_stats_alert.
	TORRENT_EXPORT int find_metric_idx(string_view name);
}

#endif
