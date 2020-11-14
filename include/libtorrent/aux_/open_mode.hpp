/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
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

#ifndef TORRENT_OPEN_MODE_HPP
#define TORRENT_OPEN_MODE_HPP

#include <cstdint>
#include "libtorrent/flags.hpp"

namespace libtorrent {
namespace aux {

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
} // aux

} // libtorrent

#endif

