/*

Copyright (c) 2015-2018, Arvid Norberg
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

#ifndef TORRENT_ANNOUNCE_ENTRY_HPP_INCLUDED
#define TORRENT_ANNOUNCE_ENTRY_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp" // for time_point
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <string>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	namespace aux {
		struct session_settings;
	}

	// this class holds information about one bittorrent tracker, as it
	// relates to a specific torrent.
	struct TORRENT_EXPORT announce_entry
	{
		// constructs a tracker announce entry with ``u`` as the URL.
		announce_entry(std::string const& u);
		announce_entry();
		~announce_entry();
#if __cplusplus >= 201103L
		announce_entry(announce_entry const&) = default;
		announce_entry& operator=(announce_entry const&) = default;
#endif

		// tracker URL as it appeared in the torrent file
		std::string url;

		// the current ``&trackerid=`` argument passed to the tracker.
		// this is optional and is normally empty (in which case no
		// trackerid is sent).
		std::string trackerid;

		// if this tracker has returned an error or warning message
		// that message is stored here
		std::string message;

		// if this tracker failed the last time it was contacted
		// this error code specifies what error occurred
		error_code last_error;

		// returns the number of seconds to the next announce on this tracker.
		// ``min_announce_in()`` returns the number of seconds until we are
		// allowed to force another tracker update with this tracker.
		// 
		// If the last time this tracker was contacted failed, ``last_error`` is
		// the error code describing what error occurred.
		int next_announce_in() const;
		int min_announce_in() const;

		// the time of next tracker announce
		time_point next_announce;

		// no announces before this time
		time_point min_announce;

		// TODO: include the number of peers received from this tracker, at last
		// announce

		// these are either -1 or the scrape information this tracker last
		// responded with. *incomplete* is the current number of downloaders in
		// the swarm, *complete* is the current number of seeds in the swarm and
		// *downloaded* is the cumulative number of completed downloads of this
		// torrent, since the beginning of time (from this tracker's point of
		// view).

		// if this tracker has returned scrape data, these fields are filled in
		// with valid numbers. Otherwise they are set to -1. the number of
		// current downloaders
		int scrape_incomplete;
		int scrape_complete;
		int scrape_downloaded;

		// the tier this tracker belongs to
		boost::uint8_t tier;

		// the max number of failures to announce to this tracker in
		// a row, before this tracker is not used anymore. 0 means unlimited
		boost::uint8_t fail_limit;

		// the number of times in a row we have failed to announce to this
		// tracker.
		boost::uint8_t fails:7;

		// true while we're waiting for a response from the tracker.
		bool updating:1;

		// flags for the source bitmask, each indicating where
		// we heard about this tracker
		enum tracker_source
		{
			// the tracker was part of the .torrent file
			source_torrent = 1,
			// the tracker was added programatically via the add_troacker()_ function
			source_client = 2,
			// the tracker was part of a magnet link
			source_magnet_link = 4,
			// the tracker was received from the swarm via tracker exchange
			source_tex = 8
		};

		// a bitmask specifying which sources we got this tracker from.
		boost::uint8_t source:4;

		// set to true the first time we receive a valid response
		// from this tracker.
		bool verified:1;

		// set to true when we get a valid response from an announce
		// with event=started. If it is set, we won't send start in the subsequent
		// announces.
		bool start_sent:1;

		// set to true when we send a event=completed.
		bool complete_sent:1;

		// this is false the stats sent to this tracker will be 0
		bool send_stats:1;

		// internal
		bool triggered_manually:1;

		// reset announce counters and clears the started sent flag.
		// The announce_entry will look like we've never talked to
		// the tracker.
		void reset();

		// updates the failure counter and time-outs for re-trying.
		// This is called when the tracker announce fails.
		void failed(aux::session_settings const& sett, int retry_interval = 0);

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.0
		TORRENT_DEPRECATED
		bool will_announce(time_point now) const
		{
			return now <= next_announce
				&& (fails < fail_limit || fail_limit == 0)
				&& !updating;
		}
#endif

		// returns true if we can announce to this tracker now.
		// The current time is passed in as ``now``. The ``is_seed``
		// argument is necessary because once we become a seed, we
		// need to announce right away, even if the re-announce timer
		// hasn't expired yet.
		bool can_announce(time_point now, bool is_seed) const;

		// returns true if the last time we tried to announce to this
		// tracker succeeded, or if we haven't tried yet.
		bool is_working() const
		{ return fails == 0; }

		// trims whitespace characters from the beginning of the URL.
		void trim();
	};

}

#endif

