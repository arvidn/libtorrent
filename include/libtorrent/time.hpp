/*

Copyright (c) 2007-2016, Arvid Norberg
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

#ifndef TORRENT_TIME_HPP_INCLUDED
#define TORRENT_TIME_HPP_INCLUDED

#include <boost/version.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/ptime.hpp"
#include <boost/cstdint.hpp>
#include <string>

// OVERVIEW
// 
// This section contains fundamental time types used internally by
// libtorrent and exposed through various places in the API. The two
// basic types are ``ptime`` and ``time_duration``. The first represents
// a point in time and the second the difference between two points
// in time.
//
// The internal representation of these types is implementation defined
// and they can only be constructed via one of the construction functions
// that take a well defined time unit (seconds, minutes, etc.). They can
// only be turned into well defined time units by the accessor functions
// (total_microseconds(), etc.).
//
// .. note::
// 	In a future version of libtorrent, these types will be replaced
// 	by the standard timer types from ``std::chrono``.
//

namespace libtorrent
{
	TORRENT_EXTRA_EXPORT char const* time_now_string();
	std::string log_time();

	// returns the current time, as represented by ptime. The
	// resolution of this timer is about 100 ms.
	TORRENT_EXPORT ptime const& time_now();

	// returns the current time as represented by ptime. This is
	// more expensive than time_now(), but provides as high resolution
	// as the operating system can provide.
	TORRENT_EXPORT ptime time_now_hires();

	// the earliest and latest possible time points
	// representable by ptime.
	TORRENT_EXPORT ptime min_time();
	TORRENT_EXPORT ptime max_time();

#if defined TORRENT_USE_BOOST_DATE_TIME || defined TORRENT_USE_QUERY_PERFORMANCE_TIMER

	// returns a time_duration representing the specified number of seconds, milliseconds
	// microseconds, minutes and hours.
	TORRENT_EXPORT time_duration seconds(boost::int64_t s);
	TORRENT_EXPORT time_duration milliseconds(boost::int64_t s);
	TORRENT_EXPORT time_duration microsec(boost::int64_t s);
	TORRENT_EXPORT time_duration minutes(boost::int64_t s);
	TORRENT_EXPORT time_duration hours(boost::int64_t s);

	// returns the number of seconds, milliseconds and microseconds
	// a time_duration represents.
	TORRENT_EXPORT boost::int64_t total_seconds(time_duration td);
	TORRENT_EXPORT boost::int64_t total_milliseconds(time_duration td);
	TORRENT_EXPORT boost::int64_t total_microseconds(time_duration td);

#elif TORRENT_USE_CLOCK_GETTIME || TORRENT_USE_SYSTEM_TIME || TORRENT_USE_ABSOLUTE_TIME

	// hidden
	inline int total_seconds(time_duration td)
	{ return td.diff / 1000000; }
	// hidden
	inline int total_milliseconds(time_duration td)
	{ return td.diff / 1000; }
	// hidden
	inline boost::int64_t total_microseconds(time_duration td)
	{ return td.diff; }

	// hidden
	inline time_duration microsec(boost::int64_t s)
	{ return time_duration(s); }
	// hidden
	inline time_duration milliseconds(boost::int64_t s)
	{ return time_duration(s * 1000); }
	// hidden
	inline time_duration seconds(boost::int64_t s)
	{ return time_duration(s * 1000000); }
	// hidden
	inline time_duration minutes(boost::int64_t s)
	{ return time_duration(s * 1000000 * 60); }
	// hidden
	inline time_duration hours(boost::int64_t s)
	{ return time_duration(s * 1000000 * 60 * 60); }

#endif // TORRENT_USE_CLOCK_GETTIME

}

#endif // TORRENT_TIME_HPP_INCLUDED

