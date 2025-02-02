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

	tcp::endpoint peer_info::remote_endpoint() const
	{
#if TORRENT_USE_I2P
		if (flags & i2p_socket) return {};
#endif
		TORRENT_ASSERT(std::holds_alternative<ip_endpoint>(m_endpoint));
		return std::get<ip_endpoint>(m_endpoint).remote;
	}

	tcp::endpoint peer_info::local_endpoint() const
	{
#if TORRENT_USE_I2P
		if (flags & i2p_socket) return {};
#endif
		TORRENT_ASSERT(std::holds_alternative<ip_endpoint>(m_endpoint));
		return std::get<ip_endpoint>(m_endpoint).local;
	}

	void peer_info::set_endpoints(tcp::endpoint const& local, tcp::endpoint const& remote)
	{
		TORRENT_ASSERT(!(flags & i2p_socket));
		m_endpoint = ip_endpoint{remote, local};
#if TORRENT_ABI_VERSION < 4
		ip = remote;
#endif
	}

#if TORRENT_USE_I2P
	sha256_hash peer_info::i2p_destination() const
	{
		sha256_hash ret;
		if (!(flags & i2p_socket)) return ret;
		TORRENT_ASSERT(std::holds_alternative<sha256_hash>(m_endpoint));
		return std::get<sha256_hash>(m_endpoint);
	}

	void peer_info::set_i2p_destination(sha256_hash dest)
	{
		flags |= i2p_socket;
		m_endpoint = dest;
	}
#endif
}
