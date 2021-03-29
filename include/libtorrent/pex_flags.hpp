/*

Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEX_FLAGS_HPP_INCLUDE
#define TORRENT_PEX_FLAGS_HPP_INCLUDE

#include <cstdint>

#include "libtorrent/flags.hpp"

namespace lt {

	using pex_flags_t = flags::bitfield_flag<std::uint8_t, struct pex_flags_tag>;

	// the peer supports protocol encryption
	constexpr pex_flags_t pex_encryption = 0_bit;

	// the peer is a seed
	constexpr pex_flags_t pex_seed = 1_bit;

	// the peer supports the uTP, transport protocol over UDP.
	constexpr pex_flags_t pex_utp = 2_bit;

	// the peer supports the holepunch extension If this flag is received from a
	// peer, it can be used as a rendezvous point in case direct connections to
	// the peer fail
	constexpr pex_flags_t pex_holepunch = 3_bit;

	// protocol v2
	// this is not a standard flag, it is only used internally
	constexpr pex_flags_t pex_lt_v2 = 7_bit;
}

#endif

