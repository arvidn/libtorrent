/*

Copyright (c) 2014, 2017-2020, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/flags.hpp"

using test_flags_t = lt::flags::bitfield_flag<std::uint32_t, struct test_flags_tag>;

namespace test_flags
{
	using lt::operator "" _bit;
	constexpr test_flags_t super_seeding = 1_bit;
	constexpr test_flags_t strict_super_seeding = 2_bit;
	constexpr test_flags_t seed_mode = 3_bit;
	constexpr test_flags_t time_critical = 4_bit;
	constexpr test_flags_t suggest = 5_bit;
	constexpr test_flags_t v1_meta = 6_bit;
	constexpr test_flags_t v2_meta = 7_bit;
}

EXPORT void test_swarm(test_flags_t flags = test_flags_t{});
