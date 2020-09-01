/*

Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_AUX_ANNOUNCE_ENTRY_HPP_INCLUDED
#define TORRENT_AUX_ANNOUNCE_ENTRY_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/info_hash.hpp"

#include <string>
#include <cstdint>
#include <vector>

namespace libtorrent {
	struct torrent;
namespace aux {

	struct TORRENT_EXTRA_EXPORT announce_infohash
	{
		announce_infohash();

		// if this tracker has returned an error or warning message
		// that message is stored here
		std::string message;

		// if this tracker failed the last time it was contacted
		// this error code specifies what error occurred
		error_code last_error;

		// the time of next tracker announce
		time_point32 next_announce = (time_point32::min)();

		// no announces before this time
		time_point32 min_announce = (time_point32::min)();

		// TODO: include the number of peers received from this tracker, at last
		// announce

		// these are either -1 or the scrape information this tracker last
		// responded with. *incomplete* is the current number of downloaders in
		// the swarm, *complete* is the current number of seeds in the swarm and
		// *downloaded* is the cumulative number of completed downloads of this
		// torrent, since the beginning of time (from this tracker's point of
		// view).

		// if this tracker has returned scrape data, these fields are filled in
		// with valid numbers. Otherwise they are set to -1. ``incomplete`` counts
		// the number of current downloaders. ``complete`` counts the number of
		// current peers completed the download, or "seeds". ``downloaded`` is the
		// cumulative number of completed downloads.
		int scrape_incomplete = -1;
		int scrape_complete = -1;
		int scrape_downloaded = -1;

		// the number of times in a row we have failed to announce to this
		// tracker.
		std::uint8_t fails : 7;

		// true while we're waiting for a response from the tracker.
		bool updating : 1;

		// set to true when we get a valid response from an announce
		// with event=started. If it is set, we won't send start in the subsequent
		// announces.
		bool start_sent : 1;

		// set to true when we send a event=completed.
		bool complete_sent : 1;

		// internal
		bool triggered_manually : 1;

		// reset announce counters and clears the started sent flag.
		// The announce_endpoint will look like we've never talked to
		// the tracker.
		void reset();

		// updates the failure counter and time-outs for re-trying.
		// This is called when the tracker announce fails.
		void failed(int backoff_ratio, seconds32 retry_interval = seconds32(0));

		// returns true if we can announce to this tracker now.
		// The current time is passed in as ``now``. The ``is_seed``
		// argument is necessary because once we become a seed, we
		// need to announce right away, even if the re-announce timer
		// hasn't expired yet.
		bool can_announce(time_point now, bool is_seed, std::uint8_t fail_limit) const;

		// returns true if the last time we tried to announce to this
		// tracker succeeded, or if we haven't tried yet.
		bool is_working() const { return fails == 0; }
	};

	struct announce_entry;

	// announces are sent to each tracker using every listen socket
	// this class holds information about one listen socket for one tracker
	struct TORRENT_EXTRA_EXPORT announce_endpoint
	{
		// internal
		announce_endpoint(aux::listen_socket_handle const& s, bool completed);

		// the local endpoint of the listen interface associated with this endpoint
		tcp::endpoint local_endpoint;

		// torrents can be announced using multiple info hashes
		// for different protocol versions

		// info_hashes[0] is the v1 info hash (SHA1)
		// info_hashes[1] is the v2 info hash (truncated SHA-256)
		aux::array<announce_infohash, num_protocols, protocol_version> info_hashes;

		// reset announce counters and clears the started sent flag.
		// The announce_endpoint will look like we've never talked to
		// the tracker.
		void reset();

		// set to false to not announce from this endpoint
		bool enabled : 1;

		// internal
		aux::listen_socket_handle socket;
	};

	// this class holds information about one bittorrent tracker, as it
	// relates to a specific torrent.
	struct TORRENT_EXTRA_EXPORT announce_entry
	{
		// constructs a tracker announce entry with ``u`` as the URL.
		explicit announce_entry(string_view u);

		// constructs the internal announce entry from the user facing one
		explicit announce_entry(lt::announce_entry const&);
		announce_entry();
		~announce_entry();
		announce_entry(announce_entry const&);
		announce_entry& operator=(announce_entry const&) &;

		// tracker URL as it appeared in the torrent file
		std::string url;

		// the current ``&trackerid=`` argument passed to the tracker.
		// this is optional and is normally empty (in which case no
		// trackerid is sent).
		std::string trackerid;

		// each local listen socket (endpoint) will announce to the tracker. This
		// list contains state per endpoint.
		std::vector<announce_endpoint> endpoints;

		// the tier this tracker belongs to
		std::uint8_t tier = 0;

		// the max number of failures to announce to this tracker in
		// a row, before this tracker is not used anymore. 0 means unlimited
		std::uint8_t fail_limit = 0;

		// a bitmask specifying which sources we got this tracker from.
		std::uint8_t source:4;

		// set to true the first time we receive a valid response
		// from this tracker.
		bool verified:1;

		// reset announce counters and clears the started sent flag.
		// The announce_entry will look like we've never talked to
		// the tracker.
		void reset();

		// internal
		announce_endpoint* find_endpoint(aux::listen_socket_handle const& s);
	};

}
}

#endif

