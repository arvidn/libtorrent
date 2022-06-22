/*

Copyright (c) 2021, Alden Torres
Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TRACKER_EVENT_HPP_INCLUDED
#define TORRENT_TRACKER_EVENT_HPP_INCLUDED

#include <cstdint>

namespace libtorrent {

// Kinds of tracker announces. This is typically indicated as the ``&event=``
// HTTP query string parameter to HTTP trackers.
enum class event_t : std::uint8_t
{
	none,
	completed,
	started,
	stopped,
	paused
};

}

#endif // TORRENT_TRACKER_EVENT_HPP_INCLUDED
