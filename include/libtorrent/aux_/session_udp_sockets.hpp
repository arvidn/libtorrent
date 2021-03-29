/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SESSION_UDP_SOCKETS_HPP_INCLUDED
#define TORRENT_SESSION_UDP_SOCKETS_HPP_INCLUDED

#include "libtorrent/aux_/utp_socket_manager.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/aux_/allocating_handler.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include <boost/asio/io_context.hpp>
#include <vector>

namespace lt {

	struct alert_manager;

namespace aux {

	struct listen_endpoint_t;
	struct proxy_settings;
	struct listen_socket_t;

	enum class transport : std::uint8_t { plaintext, ssl };

	struct session_udp_socket
	{
		explicit session_udp_socket(io_context& ios, listen_socket_handle ls)
			: sock(ios, std::move(ls)) {}

		udp::endpoint local_endpoint() { return sock.local_endpoint(); }

		udp_socket sock;

		// since udp packets are expected to be dispatched frequently, this saves
		// time on handler allocation every time we read again.
		aux::handler_storage<aux::utp_handler_max_size, utp_handler> udp_handler_storage;

		// this is true when the udp socket send() has failed with EAGAIN or
		// EWOULDBLOCK. i.e. we're currently waiting for the socket to become
		// writeable again. Once it is, we'll set it to false and notify the utp
		// socket manager
		bool write_blocked = false;
	};

} }

#endif
