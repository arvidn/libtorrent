/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Alden Torres
Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_LISTEN_SOCKET_HANDLE_HPP_INCLUDED
#define TORRENT_LISTEN_SOCKET_HANDLE_HPP_INCLUDED

#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include <memory>

namespace lt::aux {

	struct listen_socket_t;

	struct TORRENT_EXTRA_EXPORT listen_socket_handle
	{
		friend struct session_impl;

		listen_socket_handle() = default;

		listen_socket_handle(std::shared_ptr<listen_socket_t> s) // NOLINT
			: m_sock(s)
		{}

		listen_socket_handle(listen_socket_handle const& o) = default;
		listen_socket_handle& operator=(listen_socket_handle const& o) = default;
		listen_socket_handle(listen_socket_handle&& o) = default;
		listen_socket_handle& operator=(listen_socket_handle&& o) = default;

		explicit operator bool() const { return !m_sock.expired(); }

		address get_external_address() const;
		tcp::endpoint get_local_endpoint() const;
		bool can_route(address const&) const;

		bool is_ssl() const;

		bool operator==(listen_socket_handle const& o) const
		{
			return !m_sock.owner_before(o.m_sock) && !o.m_sock.owner_before(m_sock);
		}

		bool operator!=(listen_socket_handle const& o) const
		{
			return !(*this == o);
		}

		bool operator<(listen_socket_handle const& o) const
		{ return m_sock.owner_before(o.m_sock); }

		listen_socket_t* get() const;

		std::weak_ptr<listen_socket_t> get_ptr() const { return m_sock; }

	private:
		std::weak_ptr<listen_socket_t> m_sock;
	};

}

#endif
