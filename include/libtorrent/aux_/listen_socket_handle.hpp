/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Alden Torres
Copyright (c) 2020, 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_LISTEN_SOCKET_HANDLE_HPP_INCLUDED
#define TORRENT_LISTEN_SOCKET_HANDLE_HPP_INCLUDED

#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include <cstdint>
#include <functional>
#include <memory>

namespace libtorrent { namespace aux {

	struct listen_socket_t;

	struct TORRENT_EXTRA_EXPORT listen_socket_handle
	{
		friend struct session_impl;

		listen_socket_handle() = default;

		listen_socket_handle(std::shared_ptr<listen_socket_t> s); // NOLINT

		listen_socket_handle(listen_socket_handle const& o) = default;
		listen_socket_handle& operator=(listen_socket_handle const& o) = default;
		listen_socket_handle(listen_socket_handle&& o) = default;
		listen_socket_handle& operator=(listen_socket_handle&& o) = default;

		explicit operator bool() const { return !m_sock.expired(); }

		address get_external_address() const;
		tcp::endpoint get_local_endpoint() const;
		bool can_route(address const&) const;

		std::string device() const;

		bool is_ssl() const;

		// compares via m_id, not m_sock, so identity survives after the
		// listen_socket_t is destroyed and m_sock has expired.
		bool operator==(listen_socket_handle const& o) const { return m_id == o.m_id; }

		bool operator!=(listen_socket_handle const& o) const
		{
			return !(*this == o);
		}

		bool operator<(listen_socket_handle const& o) const { return m_id < o.m_id; }

		listen_socket_t* get() const;

		std::weak_ptr<listen_socket_t> get_ptr() const { return m_sock; }

		std::size_t hash_value() const { return std::hash<std::uint32_t>{}(m_id); }

	private:
		std::weak_ptr<listen_socket_t> m_sock;
		std::uint32_t m_id = 0;
	};

} }

#endif
