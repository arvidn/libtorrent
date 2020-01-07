/*

Copyright (c) 2017, Arvid Norberg, Steven Siloti
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

#ifndef TORRENT_SESSION_UDP_SOCKETS_HPP_INCLUDED
#define TORRENT_SESSION_UDP_SOCKETS_HPP_INCLUDED

#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/aux_/allocating_handler.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include <boost/asio/io_service.hpp>
#include <vector>

namespace libtorrent {

	class alert_manager;

namespace aux {

	struct listen_endpoint_t;
	struct proxy_settings;
	struct listen_socket_t;

	enum class transport : std::uint8_t { plaintext, ssl };

	struct session_udp_socket
	{
		explicit session_udp_socket(io_service& ios, listen_socket_handle ls)
			: sock(ios, std::move(ls)) {}

		udp::endpoint local_endpoint() { return sock.local_endpoint(); }

		udp_socket sock;

		// since udp packets are expected to be dispatched frequently, this saves
		// time on handler allocation every time we read again.
		aux::handler_storage<TORRENT_READ_HANDLER_MAX_SIZE> udp_handler_storage;

		// this is true when the udp socket send() has failed with EAGAIN or
		// EWOULDBLOCK. i.e. we're currently waiting for the socket to become
		// writeable again. Once it is, we'll set it to false and notify the utp
		// socket manager
		bool write_blocked = false;
	};

} }

#endif
