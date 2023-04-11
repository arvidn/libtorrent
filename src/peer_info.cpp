/*

Copyright (c) 2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/peer_info.hpp"

namespace libtorrent {

	peer_info::peer_info() = default;
	peer_info::~peer_info() = default;
	peer_info::peer_info(peer_info const&) = default;
	peer_info::peer_info(peer_info&&) = default;
	peer_info& peer_info::operator=(peer_info const&) = default;

#if TORRENT_USE_I2P
	sha256_hash peer_info::i2p_destination() const
	{
		sha256_hash ret;
		if (!(flags & i2p_socket)) return ret;

		char const* destination = reinterpret_cast<char const*>(&ip);
		static_assert(sizeof(tcp::endpoint) * 2 >= sizeof(sha256_hash), "tcp::endpoint is smaller than expected");

		std::memcpy(ret.data(), destination, ret.size());
		return ret;
	}

	void peer_info::set_i2p_destination(sha256_hash dest)
	{
		flags |= i2p_socket;
		char* destination = reinterpret_cast<char*>(&ip);
		static_assert(sizeof(tcp::endpoint) * 2 >= sizeof(sha256_hash), "tcp::endpoint is smaller than expected");

		std::memcpy(destination, dest.data(), dest.size());
	}
#endif
}
