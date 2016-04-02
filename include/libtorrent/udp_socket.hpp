/*

Copyright (c) 2007-2016, Arvid Norberg
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
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/allocating_handler.hpp"

#include <deque>

namespace libtorrent
{
	struct TORRENT_EXTRA_EXPORT udp_socket_observer
	{
		// return true if the packet was handled (it won't be
		// propagated to the next observer)
		virtual bool incoming_packet(error_code const& ec
			, udp::endpoint const&, char const* buf, int size) = 0;
		virtual bool incoming_packet(error_code const& /* ec */
			, char const* /* hostname */, char const* /* buf */, int /* size */) { return false; }

		// called when the socket becomes writeable, after having
		// failed with EWOULDBLOCK
		virtual void writable() {}

		// called every time the socket is drained of packets
		virtual void socket_drained() {}
	protected:
		~udp_socket_observer() {}
	};

	class TORRENT_EXTRA_EXPORT udp_socket : single_threaded
	{
	public:
		udp_socket(io_service& ios);
		~udp_socket();

		enum flags_t {
			dont_drop = 1
			, peer_connection = 2
			, tracker_connection = 4
			, dont_queue = 8
		};

		bool is_open() const { return m_abort == false; }
		io_service& get_io_service() { return m_ipv4_sock.get_io_service(); }

		void subscribe(udp_socket_observer* o);
		void unsubscribe(udp_socket_observer* o);

		// this is only valid when using a socks5 proxy
		void send_hostname(char const* hostname, int port, char const* p
			, int len, error_code& ec, int flags = 0);

		void send(udp::endpoint const& ep, char const* p, int len
			, error_code& ec, int flags = 0);
		void bind(udp::endpoint const& ep, error_code& ec);
		void close();
		int local_port() const { return m_bind_port; }

		void set_proxy_settings(aux::proxy_settings const& ps);
		aux::proxy_settings const& get_proxy_settings() { return m_proxy_settings; }
		void set_force_proxy(bool f) { m_force_proxy = f; }

		bool is_closed() const { return m_abort; }
		tcp::endpoint local_endpoint(error_code& ec) const
		{
			udp::endpoint ep = m_ipv4_sock.local_endpoint(ec);
			return tcp::endpoint(ep.address(), ep.port());
		}

		void set_buf_size(int s);

		typedef udp::socket::receive_buffer_size receive_buffer_size;
		typedef udp::socket::send_buffer_size send_buffer_size;

		template <class SocketOption>
		void get_option(SocketOption const& opt, error_code& ec)
		{
#if TORRENT_USE_IPV6
			if (opt.level(udp::v6()) == IPPROTO_IPV6)
				m_ipv6_sock.get_option(opt, ec);
			else
#endif
				m_ipv4_sock.get_option(opt, ec);
		}

		template <class SocketOption>
		void set_option(SocketOption const& opt, error_code& ec)
		{
			if (opt.level(udp::v4()) != IPPROTO_IPV6)
				m_ipv4_sock.set_option(opt, ec);
#if TORRENT_USE_IPV6
			if (opt.level(udp::v6()) != IPPROTO_IP)
				m_ipv6_sock.set_option(opt, ec);
#endif
		}

		template <class SocketOption>
		void get_option(SocketOption& opt, error_code& ec)
		{
#if TORRENT_USE_IPV6
			if (opt.level(udp::v6()) == IPPROTO_IPV6)
				m_ipv6_sock.get_option(opt, ec);
			else
#endif
				m_ipv4_sock.get_option(opt, ec);
		}

		udp::endpoint proxy_addr() const { return m_proxy_addr; }

	private:

		struct queued_packet
		{
			queued_packet()
				: hostname(NULL)
				, flags(0)
			{}

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

		// non-copyable
		udp_socket(udp_socket const&);
		udp_socket& operator=(udp_socket const&);

		void close_impl();

		// observers on this udp socket
		std::vector<udp_socket_observer*> m_observers;
		std::vector<udp_socket_observer*> m_added_observers;

		template <class Handler>
		aux::allocating_handler<Handler, TORRENT_READ_HANDLER_MAX_SIZE>
			make_read_handler4(Handler const& handler)
		{
			return aux::allocating_handler<Handler, TORRENT_READ_HANDLER_MAX_SIZE>(
				handler, m_v4_read_handler_storage
			);
		}

#if TORRENT_USE_IPV6
		template <class Handler>
		aux::allocating_handler<Handler, TORRENT_READ_HANDLER_MAX_SIZE>
			make_read_handler6(Handler const& handler)
		{
			return aux::allocating_handler<Handler, TORRENT_READ_HANDLER_MAX_SIZE>(
				handler, m_v6_read_handler_storage
			);
		}
#endif

		// this is true while iterating over the observers
		// vector, invoking observer hooks. We may not
		// add new observers during this time, since it
		// may invalidate the iterator. If this is true,
		// instead add new observers to m_added_observers
		// and they will be added later
		bool m_observers_locked;

		void call_handler(error_code const& ec, udp::endpoint const& ep
			, char const* buf, int size);
		void call_handler(error_code const& ec, const char* host
			, char const* buf, int size);
		void call_drained_handler();
		void call_writable_handler();

		void on_writable(error_code const& ec, udp::socket* s);

		void setup_read(udp::socket* s);
		void on_read(error_code const& ec, udp::socket* s);
		void on_read_impl(udp::endpoint const& ep
			, error_code const& e, std::size_t bytes_transferred);
		void on_name_lookup(error_code const& e, tcp::resolver::iterator i);
		void on_connect_timeout(error_code const& ec);
		void on_connected(error_code const& ec);
		void handshake1(error_code const& e);
		void handshake2(error_code const& e);
		void handshake3(error_code const& e);
		void handshake4(error_code const& e);
		void socks_forward_udp();
		void connect1(error_code const& e);
		void connect2(error_code const& e);
		void hung_up(error_code const& e);

		void drain_queue();

		void wrap(udp::endpoint const& ep, char const* p, int len, error_code& ec);
		void wrap(char const* hostname, int port, char const* p, int len, error_code& ec);
		void unwrap(error_code const& e, char const* buf, int size);

		udp::socket m_ipv4_sock;
		aux::handler_storage<TORRENT_READ_HANDLER_MAX_SIZE> m_v4_read_handler_storage;
		deadline_timer m_timer;
		int m_buf_size;

		// if the buffer size is attempted
		// to be changed while the buffer is
		// being used, this member is set to
		// the desired size, and it's resized
		// later
		int m_new_buf_size;
		char* m_buf;

#if TORRENT_USE_IPV6
		udp::socket m_ipv6_sock;
		aux::handler_storage<TORRENT_READ_HANDLER_MAX_SIZE> m_v6_read_handler_storage;
#endif

		boost::uint16_t m_bind_port;
		boost::uint8_t m_v4_outstanding;
		boost::uint8_t m_restart_v4;
#if TORRENT_USE_IPV6
		boost::uint8_t m_v6_outstanding;
		boost::uint8_t m_restart_v6;
#endif

		tcp::socket m_socks5_sock;
		aux::proxy_settings m_proxy_settings;
		tcp::resolver m_resolver;
		char m_tmp_buf[270];
		bool m_queue_packets;
		bool m_tunnel_packets;
		bool m_force_proxy;
		bool m_abort;

		// this is the endpoint the proxy server lives at.
		// when performing a UDP associate, we get another
		// endpoint (presumably on the same IP) where we're
		// supposed to send UDP packets.
		udp::endpoint m_proxy_addr;

		// this is where UDP packets that are to be forwarded
		// are sent. The result from UDP ASSOCIATE is stored
		// in here.
		udp::endpoint m_udp_proxy_addr;

		// while we're connecting to the proxy
		// we have to queue the packets, we'll flush
		// them once we're connected
		std::deque<queued_packet> m_queue;

		// counts the number of outstanding async
		// operations hanging on this socket
		int m_outstanding_ops;

#if TORRENT_USE_IPV6
		bool m_v6_write_subscribed:1;
#endif
		bool m_v4_write_subscribed:1;

#if TORRENT_USE_ASSERTS
		bool m_started;
		int m_magic;
		int m_outstanding_when_aborted;
		int m_outstanding_connect;
		int m_outstanding_timeout;
		int m_outstanding_resolve;
		int m_outstanding_socks;
#endif
	};

	struct rate_limited_udp_socket : public udp_socket
	{
		rate_limited_udp_socket(io_service& ios);
		void set_rate_limit(int limit) { m_rate_limit = limit; }
		bool send(udp::endpoint const& ep, char const* p, int len
			, error_code& ec, int flags = 0);
		bool has_quota();

	private:

		int m_rate_limit;
		int m_quota;
		time_point m_last_tick;
	};
}

#endif

