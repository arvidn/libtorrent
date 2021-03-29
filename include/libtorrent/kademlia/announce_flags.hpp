/*

Copyright (c) 2018-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef ANNOUNCE_FLAGS_HPP
#define ANNOUNCE_FLAGS_HPP

#include <cstdint>

#include "libtorrent/flags.hpp"

namespace lt {
namespace dht {

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

}
}

#endif
