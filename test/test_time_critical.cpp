/*

Copyright (c) 2014-2015, 2017, 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "swarm_suite.hpp"

TORRENT_TEST(time_crititcal)
{
	// with time critical pieces
	test_swarm(test_flags::time_critical);
}


