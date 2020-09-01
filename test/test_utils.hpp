/*

Copyright (c) 2015, 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TEST_UTILS_HPP
#define TEST_UTILS_HPP

#include "test.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/download_priority.hpp"

namespace libtorrent
{
	EXPORT char const* time_now_string();
	EXPORT char const* time_to_string(lt::time_point const tp);
}

inline lt::download_priority_t operator "" _pri(unsigned long long const p)
{
	return lt::download_priority_t(static_cast<std::uint8_t>(p));
}

#endif

