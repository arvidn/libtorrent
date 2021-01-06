/*

Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TRACKER_EVENT_HPP_INCLUDED
#define TORRENT_TRACKER_EVENT_HPP_INCLUDED

#include <cstdint>

namespace libtorrent {

// Specifies what event was sent to the tracker. It is defined as:
//
// 0. None
// 1. Completed
// 2. Started
// 3. Stopped
// 4. Paused
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
