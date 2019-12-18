/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef TORRENT_PEX_FLAGS_HPP_INCLUDE
#define TORRENT_PEX_FLAGS_HPP_INCLUDE

#include <cstdint>

#include "libtorrent/flags.hpp"

namespace libtorrent {

	using pex_flags_t = flags::bitfield_flag<std::uint8_t, struct pex_flags_tag>;

	// the peer supports protocol encryption
	constexpr pex_flags_t pex_encryption = 1_bit;

	// the peer is a seed
	constexpr pex_flags_t pex_seed = 2_bit;

	// the peer supports the uTP, transport protocol over UDP.
	constexpr pex_flags_t pex_utp = 3_bit;

	// the peer supports the holepunch extension If this flag is received from a
	// peer, it can be used as a rendezvous point in case direct connections to
	// the peer fail
	constexpr pex_flags_t pex_holepunch = 4_bit;
}

#endif

