/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_OPEN_MODE_HPP
#define TORRENT_OPEN_MODE_HPP

#include <cstdint>
#include "libtorrent/flags.hpp"

namespace lt::aux {

	// hidden
	using open_mode_t = flags::bitfield_flag<std::uint32_t, struct open_mode_tag>;

	namespace open_mode {
		constexpr open_mode_t read_only{0};
		constexpr open_mode_t write = 0_bit;
		constexpr open_mode_t no_cache = 1_bit;
		constexpr open_mode_t truncate = 2_bit;
		constexpr open_mode_t no_atime = 3_bit;
		constexpr open_mode_t random_access = 4_bit;
		constexpr open_mode_t hidden = 5_bit;
		constexpr open_mode_t sparse = 6_bit;
		constexpr open_mode_t executable = 7_bit;
		constexpr open_mode_t allow_set_file_valid_data = 8_bit;
	}
} // lt::aux

#endif

