/*

Copyright (c) 2009, 2015, 2017-2018, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/time.hpp"

#include <chrono>

namespace libtorrent { namespace aux {

	time_point time_now() { return clock_type::now(); }
	time_point32 time_now32() { return time_point_cast<seconds32>(clock_type::now()); }

	// for simplifying implementation
	using std::chrono::system_clock;


	// consider using std::chrono::clock_cast on C++20
	time_t to_time_t(const time_point32 tp)
	{
		// special case for unset value
		if (tp == time_point32(seconds32(0))) return 0;

		const auto lt_now = clock_type::now();
		const auto sys_now = system_clock::now();

		const auto r = sys_now + std::chrono::duration_cast<system_clock::duration>(tp - lt_now) + lt::milliseconds(500);
		return system_clock::to_time_t(r);
	}

	// consider using std::chrono::clock_cast on C++20
	time_point32 from_time_t(const std::time_t t)
	{
		// special case for unset value
		if (t == 0) return time_point32(seconds32(0));

		const auto tp = system_clock::from_time_t(t);
		const auto sys_now = system_clock::now();
		const auto lt_now = clock_type::now();

		auto r = lt_now + std::chrono::duration_cast<clock_type::duration>(tp - sys_now);
		// the conversion to seconds will truncate, make sure we round
		return std::chrono::time_point_cast<seconds32>(r + milliseconds(500));
	}

} }
