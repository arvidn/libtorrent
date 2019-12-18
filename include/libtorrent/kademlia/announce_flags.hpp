/*

Copyright (c) 2018, Arvid Norberg
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

#ifndef ANNOUNCE_FLAGS_HPP
#define ANNOUNCE_FLAGS_HPP

#include <cstdint>

#include "libtorrent/flags.hpp"

namespace libtorrent { namespace dht {

using announce_flags_t = flags::bitfield_flag<std::uint8_t, struct dht_announce_flag_tag>;

namespace announce {

// announce to DHT as a seed
constexpr announce_flags_t seed = 0_bit;

// announce to DHT with the implied-port flag set. This tells the network to use
// your source UDP port as your listen port, rather than the one specified in
// the message. This may improve the chances of traversing NATs when using uTP.
constexpr announce_flags_t implied_port = 1_bit;

// Specify the port number for the SSL listen socket in the DHT announce.
constexpr announce_flags_t ssl_torrent = 2_bit;

}

}}

#endif
