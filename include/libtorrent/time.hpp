/*

Copyright (c) 2007, 2009, 2014-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TIME_HPP_INCLUDED
#define TORRENT_TIME_HPP_INCLUDED

#include "libtorrent/config.hpp"

#include <cstdint>
#include <chrono>

#if defined TORRENT_BUILD_SIMULATOR
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace lt {

#if defined TORRENT_BUILD_SIMULATOR
	using clock_type = sim::chrono::high_resolution_clock;
#else
	using clock_type = std::chrono::high_resolution_clock;
#endif

	using time_point = clock_type::time_point;
	using time_duration = clock_type::duration;

	// 32 bit versions of time_point and duration, with second resolution
	using milliseconds32 = std::chrono::duration<std::int32_t, std::ratio<1, 1000>>;
	using seconds32 = std::chrono::duration<std::int32_t>;
	using minutes32 = std::chrono::duration<std::int32_t, std::ratio<60>>;
	using time_point32 = std::chrono::time_point<clock_type, seconds32>;

	using seconds = std::chrono::seconds;
	using milliseconds = std::chrono::milliseconds;
	using microseconds = std::chrono::microseconds;
	using minutes = std::chrono::minutes;
	using hours = std::chrono::hours;
	using std::chrono::duration_cast;
	using std::chrono::time_point_cast;

	// internal
	inline time_point min_time() { return (time_point::min)(); }

	// internal
	inline time_point max_time() { return (time_point::max)(); }

	template<class T>
	std::int64_t total_seconds(T td)
	{ return duration_cast<seconds>(td).count(); }

	template<class T>
	std::int64_t total_milliseconds(T td)
	{ return duration_cast<milliseconds>(td).count(); }

	template<class T>
	std::int64_t total_microseconds(T td)
	{ return duration_cast<microseconds>(td).count(); }

}

#endif // TORRENT_TIME_HPP_INCLUDED
