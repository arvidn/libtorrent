/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2015-2016, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test_utils.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{
	char const* time_now_string()
	{
		return time_to_string(clock_type::now());
	}

	char const* time_to_string(time_point const tp)
	{
		static const time_point start = clock_type::now();
		static char ret[200];
		int t = int(total_milliseconds(tp - start));
		int h = t / 1000 / 60 / 60;
		t -= h * 60 * 60 * 1000;
		int m = t / 1000 / 60;
		t -= m * 60 * 1000;
		int s = t / 1000;
		t -= s * 1000;
		int ms = t;
		std::snprintf(ret, sizeof(ret), "%02d:%02d:%02d.%03d", h, m, s, ms);
		return ret;
	}
}

