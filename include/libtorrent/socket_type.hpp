/*

Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SOCKET_TYPE_HPP
#define TORRENT_SOCKET_TYPE_HPP

#include "libtorrent/config.hpp"
#include <cstdint>

namespace lt {

// A type describing kinds of sockets involved in various operations or events.
enum class socket_type_t : std::uint8_t {
	tcp,
	socks5,
	http,
	utp,
	i2p,
	rtc,
	tcp_ssl,
	socks5_ssl,
	http_ssl,
	utp_ssl,

#if TORRENT_ABI_VERSION <= 2
	udp TORRENT_DEPRECATED_ENUM = utp,
#endif
};

// return a short human readable name for types of socket
// TODO: move to aux
char const* socket_type_name(socket_type_t);

}

#endif
