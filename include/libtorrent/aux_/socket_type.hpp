/*

Copyright (c) 2007, 2009-2010, 2012-2013, 2017-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
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

#ifndef TORRENT_SOCKET_TYPE
#define TORRENT_SOCKET_TYPE

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/http_stream.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/aux_/polymorphic_socket.hpp"
#include "libtorrent/socket_type.hpp"

#if TORRENT_USE_SSL
#include "libtorrent/ssl_stream.hpp"
#endif

#include "libtorrent/debug.hpp"

namespace libtorrent {
namespace aux {

	using socket_type = polymorphic_socket<
		tcp::socket
		, socks5_stream
		, http_stream
		, utp_stream
#if TORRENT_USE_I2P
		, i2p_stream
#endif
#if TORRENT_USE_SSL
		, ssl_stream<tcp::socket>
		, ssl_stream<socks5_stream>
		, ssl_stream<http_stream>
		, ssl_stream<utp_stream>
#endif
	>;

	// returns true if this socket is an SSL socket
	bool is_ssl(socket_type const& s);

	// returns true if this is a uTP socket
	bool is_utp(socket_type const& s);

	socket_type_t socket_type_idx(socket_type const& s);

	char const* socket_type_name(socket_type const& s);

	// this is only relevant for uTP connections
	void set_close_reason(socket_type& s, close_reason_t code);
	close_reason_t get_close_reason(socket_type const& s);

#if TORRENT_USE_I2P
	// returns true if this is an i2p socket
	bool is_i2p(socket_type const& s);
#endif

	// assuming the socket_type s is an ssl socket, make sure it
	// verifies the hostname in its SSL handshake
	void setup_ssl_hostname(socket_type& s, std::string const& hostname, error_code& ec);

	// properly shuts down SSL sockets. holder keeps s alive
	void async_shutdown(socket_type& s, std::shared_ptr<void> holder);
}
}

#endif
