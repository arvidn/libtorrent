/*

Copyright (c) 2007-2018, Arvid Norberg
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

#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"

namespace libtorrent { namespace aux {

	// TODO: 2 peer_connection and tracker_connection should probably be flags
	bool instantiate_connection(io_service& ios
		, aux::proxy_settings const& ps, aux::socket_type& s
		, void* ssl_context
		, utp_socket_manager* sm
		, bool peer_connection
		, bool tracker_connection)
	{
#ifndef TORRENT_USE_OPENSSL
		TORRENT_UNUSED(ssl_context);
#endif

		if (sm)
		{
			utp_stream* str;
#ifdef TORRENT_USE_OPENSSL
			if (ssl_context)
			{
				s.instantiate<ssl_stream<utp_stream>>(ios, ssl_context);
				str = &s.get<ssl_stream<utp_stream>>()->next_layer();
			}
			else
#endif
			{
				s.instantiate<utp_stream>(ios);
				str = s.get<utp_stream>();
			}
			str->set_impl(sm->new_utp_socket(str));
		}
#if TORRENT_USE_I2P
		else if (ps.type == settings_pack::i2p_proxy)
		{
			// it doesn't make any sense to try ssl over i2p
			TORRENT_ASSERT(ssl_context == nullptr);
			s.instantiate<i2p_stream>(ios);
			s.get<i2p_stream>()->set_proxy(ps.hostname, ps.port);
		}
#endif
		else if (ps.type == settings_pack::none
			|| (peer_connection && !ps.proxy_peer_connections)
			|| (tracker_connection && !ps.proxy_tracker_connections))
		{
#ifdef TORRENT_USE_OPENSSL
			if (ssl_context)
			{
				s.instantiate<ssl_stream<tcp::socket>>(ios, ssl_context);
			}
			else
#endif
			{
				s.instantiate<tcp::socket>(ios);
			}
		}
		else if (ps.type == settings_pack::http
			|| ps.type == settings_pack::http_pw)
		{
			http_stream* str;
#ifdef TORRENT_USE_OPENSSL
			if (ssl_context)
			{
				s.instantiate<ssl_stream<http_stream>>(ios, ssl_context);
				str = &s.get<ssl_stream<http_stream>>()->next_layer();
			}
			else
#endif
			{
				s.instantiate<http_stream>(ios);
				str = s.get<http_stream>();
			}

			str->set_proxy(ps.hostname, ps.port);
			if (ps.type == settings_pack::http_pw)
				str->set_username(ps.username, ps.password);
		}
		else if (ps.type == settings_pack::socks5
			|| ps.type == settings_pack::socks5_pw
			|| ps.type == settings_pack::socks4)
		{
			socks5_stream* str;
#ifdef TORRENT_USE_OPENSSL
			if (ssl_context)
			{
				s.instantiate<ssl_stream<socks5_stream>>(ios, ssl_context);
				str = &s.get<ssl_stream<socks5_stream>>()->next_layer();
			}
			else
#endif
			{
				s.instantiate<socks5_stream>(ios);
				str = s.get<socks5_stream>();
			}
			str->set_proxy(ps.hostname, ps.port);
			if (ps.type == settings_pack::socks5_pw)
				str->set_username(ps.username, ps.password);
			if (ps.type == settings_pack::socks4)
				str->set_version(4);
		}
		else
		{
			TORRENT_ASSERT_FAIL_VAL(ps.type);
			return false;
		}
		return true;
	}

}}
