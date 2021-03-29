/*

Copyright (c) 2007, 2010-2011, 2014-2020, Arvid Norberg
Copyright (c) 2018, 2021, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/utp_socket_manager.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/aux_/ssl_stream.hpp"

namespace lt::aux {

	// TODO: 2 peer_connection and tracker_connection should probably be flags
	aux::socket_type instantiate_connection(io_context& ios
		, aux::proxy_settings const& ps
		, void* ssl_context
		, utp_socket_manager* sm
		, bool peer_connection
		, bool tracker_connection)
	{
#if !TORRENT_USE_SSL
		TORRENT_UNUSED(ssl_context);
#endif

		if (sm)
		{
#if TORRENT_USE_SSL
			if (ssl_context)
			{
				ssl_stream<utp_stream> s(ios, *static_cast<ssl::context*>(ssl_context));
				s.next_layer().set_impl(sm->new_utp_socket(&s.next_layer()));
				return socket_type(std::move(s));
			}
			else
#endif
			{
				utp_stream s(ios);
				s.set_impl(sm->new_utp_socket(&s));
				return socket_type(std::move(s));
			}
		}
#if TORRENT_USE_I2P
		else if (ps.type == settings_pack::i2p_proxy)
		{
			// it doesn't make any sense to try ssl over i2p
			TORRENT_ASSERT(ssl_context == nullptr);
			i2p_stream s(ios);
			s.set_proxy(ps.hostname, ps.port);
			return socket_type(std::move(s));
		}
#endif
		else if (ps.type == settings_pack::none
			|| (peer_connection && !ps.proxy_peer_connections)
			|| (tracker_connection && !ps.proxy_tracker_connections))
		{
#if TORRENT_USE_SSL
			if (ssl_context)
			{
				return socket_type(ssl_stream<tcp::socket>(ios, *static_cast<ssl::context*>(ssl_context)));
			}
			else
#endif
			{
				return socket_type(tcp::socket(ios));
			}
		}
		else if (ps.type == settings_pack::http
			|| ps.type == settings_pack::http_pw)
		{
#if TORRENT_USE_SSL
			if (ssl_context)
			{
				ssl_stream<http_stream> s(ios, *static_cast<ssl::context*>(ssl_context));
				http_stream* str = &s.next_layer();
				str->set_proxy(ps.hostname, ps.port);
				if (ps.type == settings_pack::http_pw)
					str->set_username(ps.username, ps.password);
				return socket_type(std::move(s));
			}
			else
#endif
			{
				http_stream s(ios);
				s.set_proxy(ps.hostname, ps.port);
				if (ps.type == settings_pack::http_pw)
					s.set_username(ps.username, ps.password);
				return socket_type(std::move(s));
			}

		}
		else if (ps.type == settings_pack::socks5
			|| ps.type == settings_pack::socks5_pw
			|| ps.type == settings_pack::socks4)
		{
#if TORRENT_USE_SSL
			if (ssl_context)
			{
				ssl_stream<socks5_stream> s(ios, *static_cast<ssl::context*>(ssl_context));
				socks5_stream* str = &s.next_layer();
				str->set_proxy(ps.hostname, ps.port);
				if (ps.type == settings_pack::socks5_pw)
					str->set_username(ps.username, ps.password);
				if (ps.type == settings_pack::socks4)
					str->set_version(4);
				return socket_type(std::move(s));
			}
			else
#endif
			{
				socks5_stream s(ios);
				s.set_proxy(ps.hostname, ps.port);
				if (ps.type == settings_pack::socks5_pw)
					s.set_username(ps.username, ps.password);
				if (ps.type == settings_pack::socks4)
					s.set_version(4);
				return socket_type(std::move(s));
			}
		}
		TORRENT_ASSERT_FAIL();
		throw std::runtime_error("unknown socket type");
	}

}
