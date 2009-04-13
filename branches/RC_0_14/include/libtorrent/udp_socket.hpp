/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef TORRENT_UDP_SOCKET_HPP_INCLUDED
#define TORRENT_UDP_SOCKET_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/session_settings.hpp"

#include <vector>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>

namespace libtorrent
{
	class connection_queue;

	class udp_socket
	{
	public:
		typedef boost::function<void(error_code const& ec
			, udp::endpoint const&, char const* buf, int size)> callback_t;

		udp_socket(io_service& ios, callback_t const& c, connection_queue& cc);
		~udp_socket();

		bool is_open() const { return m_ipv4_sock.is_open() || m_ipv6_sock.is_open(); }
		io_service& get_io_service() { return m_ipv4_sock.get_io_service(); }

		void send(udp::endpoint const& ep, char const* p, int len, error_code& ec);
		void bind(udp::endpoint const& ep, error_code& ec);
		void bind(int port);
		void close();
		int local_port() const { return m_bind_port; }

		void set_proxy_settings(proxy_settings const& ps);
		proxy_settings const& get_proxy_settings() { return m_proxy_settings; }

	private:

		callback_t m_callback;

		void on_read(udp::socket* sock, error_code const& e, std::size_t bytes_transferred);
		void on_name_lookup(error_code const& e, tcp::resolver::iterator i);
		void on_timeout();
		void on_connect(int ticket);
		void on_connected(error_code const& ec);
		void handshake1(error_code const& e);
		void handshake2(error_code const& e);
		void handshake3(error_code const& e);
		void handshake4(error_code const& e);
		void socks_forward_udp();
		void connect1(error_code const& e);
		void connect2(error_code const& e);

		void wrap(udp::endpoint const& ep, char const* p, int len, error_code& ec);
		void unwrap(error_code const& e, char const* buf, int size);

		typedef boost::mutex mutex_t;
		mutable mutex_t m_mutex;

		udp::socket m_ipv4_sock;
		udp::socket m_ipv6_sock;
		udp::endpoint m_v4_ep;
		udp::endpoint m_v6_ep;
		char m_v4_buf[1600];
		char m_v6_buf[1600];
		int m_bind_port;
		char m_outstanding;

		tcp::socket m_socks5_sock;
		int m_connection_ticket;
		proxy_settings m_proxy_settings;
		connection_queue& m_cc;
		tcp::resolver m_resolver;
		char m_tmp_buf[100];
		bool m_tunnel_packets;
		bool m_abort;
		udp::endpoint m_proxy_addr;
#ifdef TORRENT_DEBUG
		bool m_started;
		int m_magic;
#endif
	};
}

#endif

