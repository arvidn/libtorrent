/*

Copyright (c) 2010, 2014-2018, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session_stats.hpp"
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.

using namespace lt;

int main()
{
	std::vector<stats_metric> m = session_stats_metrics();
	for (auto const& c : m)
	{
		std::printf("%s: %s (%d)\n"
			, c.type == metric_type_t::counter ? "CNTR" : "GAUG"
			, c.name, c.value_index);
	}
	return 0;
}

