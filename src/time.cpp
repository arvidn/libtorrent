/*

Copyright (c) 2009, 2015, 2017-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/time.hpp"

namespace lt::aux {

	time_point time_now() { return clock_type::now(); }
	time_point32 time_now32() { return time_point_cast<seconds32>(clock_type::now()); }

}
