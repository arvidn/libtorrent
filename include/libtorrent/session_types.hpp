/*

Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SESSION_TYPES_HPP_INCLUDED
#define TORRENT_SESSION_TYPES_HPP_INCLUDED

#include <cstdint>
#include "libtorrent/flags.hpp"

namespace lt {

	// hidden
	using save_state_flags_t = flags::bitfield_flag<std::uint32_t, struct save_state_flags_tag>;

	// hidden
	using session_flags_t = flags::bitfield_flag<std::uint8_t, struct session_flags_tag>;

	// The flags type used to specify options to removing files of torrents
	using remove_flags_t = flags::bitfield_flag<std::uint8_t, struct remove_flags_tag>;

	// hidden
	using reopen_network_flags_t = flags::bitfield_flag<std::uint8_t, struct reopen_network_flags_tag>;
}

#endif

