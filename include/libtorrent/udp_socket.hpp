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
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/deadline_timer.hpp"

#include <deque>
#include <boost/function/function4.hpp>

namespace libtorrent
{
	class connection_queue;

	class udp_socket
	{
	public:
		typedef boost::function<void(error_code const& ec
			, udp::endpoint const&, char const* buf, int size)> callback_t;
		typedef boost::function<void(error_code const& ec
			, char const*, char const* buf, int size)> callback2_t;

		udp_socket(io_service& ios, callback_t const& c, callback2_t const& c2, connection_queue& cc);
		~udp_socket();

		enum flags_t { dont_drop = 1, peer_connection = 2 };

		bool is_open() const
		{
			return m_ipv4_sock.is_open()
#if TORRENT_USE_IPV6
				|| m_ipv6_sock.is_open()
#endif
				;
		}
		io_service& get_io_service() { return m_ipv4_sock.get_io_service(); }

		// this is only valid when using a socks5 proxy
		void send_hostname(char const* hostname, int port, char const* p, int len, error_code& ec);

		void send(udp::endpoint const& ep, char const* p, int len, error_code& ec, int flags = 0);
		void bind(udp::endpoint const& ep, error_code& ec);
		void bind(int port);
		void close();
		int local_port() const { return m_bind_port; }

		void set_proxy_settings(proxy_settings const& ps);
		proxy_settings const& get_proxy_settings() { return m_proxy_settings; }

		bool is_closed() const { return m_abort; }
		tcp::endpoint local_endpoint(error_code& ec) const
		{
			udp::endpoint ep = m_ipv4_sock.local_endpoint(ec);
			return tcp::endpoint(ep.address(), ep.port());
		}

		void set_buf_size(int s);

		template <class SocketOption>
		void set_option(SocketOption const& opt, error_code& ec)
		{
			m_ipv4_sock.set_option(opt, ec);
#if TORRENT_USE_IPV6
			m_ipv6_sock.set_option(opt, ec);
#endif
		}

		template <class SocketOption>
		void get_option(SocketOption& opt, error_code& ec)
		{
			m_ipv4_sock.get_option(opt, ec);
		}

		udp::endpoint proxy_addr() const { return m_proxy_addr; }

	protected:

		struct queued_packet
		{
			udp::endpoint ep;
			char* hostname;
			buffer buf;
			int flags;
		};

		// number of outstanding UDP socket operations
		// using the UDP socket buffer
		int num_outstanding() const
		{
			return m_v4_outstanding
#if TORRENT_USE_IPV6
			  	+ m_v6_outstanding
#endif
				;
		}

	private:
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	// necessary for logging member offsets
	public:
#endif

		// callback for regular incoming packets
		callback_t m_callback;

		// callback for proxied incoming packets with a domain
		// name as source
		callback2_t m_callback2;

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
		void hung_up(error_code const& e);

		void wrap(udp::endpoint const& ep, char const* p, int len, error_code& ec);
		void wrap(char const* hostname, int port, char const* p, int len, error_code& ec);
		void unwrap(error_code const& e, char const* buf, int size);

		void maybe_realloc_buffers(int which = 3);
		bool maybe_clear_callback();

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
#if defined BOOST_HAS_PTHREADS
		mutable pthread_t m_thread;
#endif
		bool is_single_thread() const
		{
#if defined BOOST_HAS_PTHREADS
			if (m_thread == 0)
				m_thread = pthread_self();
			return m_thread == pthread_self();
#endif
			return true;
		}
#endif

		udp::socket m_ipv4_sock;
		udp::endpoint m_v4_ep;
		int m_v4_buf_size;
		char* m_v4_buf;
		// this is set to true to indicate that the
		// m_v4_buf should be reallocated to the size
		// of the buffer size members the next time their
		// read handler gets triggered
		bool m_reallocate_buffer4;

#if TORRENT_USE_IPV6
		udp::socket m_ipv6_sock;
		udp::endpoint m_v6_ep;
		int m_v6_buf_size;
		char* m_v6_buf;
		// this is set to true to indicate that the
		// m_v6_buf should be reallocated to the size
		// of the buffer size members the next time their
		// read handler gets triggered
		bool m_reallocate_buffer6;
#endif

		boost::uint16_t m_bind_port;
		boost::uint8_t m_v4_outstanding;
#if TORRENT_USE_IPV6
		boost::uint8_t m_v6_outstanding;
#endif

		tcp::socket m_socks5_sock;
		int m_connection_ticket;
		proxy_settings m_proxy_settings;
		connection_queue& m_cc;
		tcp::resolver m_resolver;
		char m_tmp_buf[270];
		bool m_queue_packets;
		bool m_tunnel_packets;
		bool m_abort;
		udp::endpoint m_proxy_addr;
		// while we're connecting to the proxy
		// we have to queue the packets, we'll flush
		// them once we're connected
		std::deque<queued_packet> m_queue;

		// counts the number of outstanding async
		// operations hanging on this socket
		int m_outstanding_ops;

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool m_started;
		int m_magic;
		int m_outstanding_when_aborted;
		int m_outstanding_connect;
		int m_outstanding_timeout;
		int m_outstanding_resolve;
		int m_outstanding_connect_queue;
		int m_outstanding_socks;
#endif
	};

	struct rate_limited_udp_socket : public udp_socket
	{
		rate_limited_udp_socket(io_service& ios, callback_t const& c, callback2_t const& c2, connection_queue& cc);
		void set_rate_limit(int limit) { m_rate_limit = limit; }
		bool send(udp::endpoint const& ep, char const* p, int len, error_code& ec, int flags = 0);

	private:

		int m_rate_limit;
		int m_quota;
		ptime m_last_tick;
	};
}

#endif

