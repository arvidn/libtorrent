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

#ifndef TORRENT_UDP_SOCKET_HPP_INCLUDED
#define TORRENT_UDP_SOCKET_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"

#include <array>
#include <memory>

namespace libtorrent {

	class alert_manager;
	struct socks5;

	using udp_send_flags_t = flags::bitfield_flag<std::uint8_t, struct udp_send_flags_tag>;

	class TORRENT_EXTRA_EXPORT udp_socket : single_threaded
	{
	public:
		explicit udp_socket(io_service& ios, aux::listen_socket_handle ls);

		static constexpr udp_send_flags_t peer_connection = 0_bit;
		static constexpr udp_send_flags_t tracker_connection = 1_bit;
		static constexpr udp_send_flags_t dont_queue = 2_bit;
		static constexpr udp_send_flags_t dont_fragment = 3_bit;

		bool is_open() const { return m_abort == false; }
		io_service& get_io_service() { return lt::get_io_service(m_socket); }

		template <typename Handler>
		void async_read(Handler&& h)
		{
			m_socket.async_receive(null_buffers(), std::forward<Handler>(h));
		}

		template <typename Handler>
		void async_write(Handler&& h)
		{
			m_socket.async_send(null_buffers(), std::forward<Handler>(h));
		}

		struct packet
		{
			span<char> data;
			udp::endpoint from;
			error_code error;
		};

		int read(span<packet> pkts, error_code& ec);

		// this is only valid when using a socks5 proxy
		void send_hostname(char const* hostname, int port, span<char const> p
			, error_code& ec, udp_send_flags_t flags = {});

		void send(udp::endpoint const& ep, span<char const> p
			, error_code& ec, udp_send_flags_t flags = {});
		void open(udp const& protocol, error_code& ec);
		void bind(udp::endpoint const& ep, error_code& ec);
		void close();
		int local_port() const { return m_bind_port; }

		void set_proxy_settings(aux::proxy_settings const& ps, alert_manager& alerts);
		aux::proxy_settings const& get_proxy_settings() { return m_proxy_settings; }

		bool is_closed() const { return m_abort; }
		udp::endpoint local_endpoint(error_code& ec) const
		{ return m_socket.local_endpoint(ec); }
		// best effort, if you want to know the error, use
		// ``local_endpoint(error_code& ec)``
		udp::endpoint local_endpoint() const
		{
			error_code ec;
			return local_endpoint(ec);
		}

		using receive_buffer_size = udp::socket::receive_buffer_size;
		using send_buffer_size = udp::socket::send_buffer_size;

		template <class SocketOption>
		void get_option(SocketOption const& opt, error_code& ec)
		{
				m_socket.get_option(opt, ec);
		}

		template <class SocketOption>
		void set_option(SocketOption const& opt, error_code& ec)
		{
			m_socket.set_option(opt, ec);
		}

		template <class SocketOption>
		void get_option(SocketOption& opt, error_code& ec)
		{
			m_socket.get_option(opt, ec);
		}

		bool active_socks5() const;

	private:

		// non-copyable
		udp_socket(udp_socket const&);
		udp_socket& operator=(udp_socket const&);

		void wrap(udp::endpoint const& ep, span<char const> p, error_code& ec, udp_send_flags_t flags);
		void wrap(char const* hostname, int port, span<char const> p, error_code& ec, udp_send_flags_t flags);
		bool unwrap(udp::endpoint& from, span<char>& buf);

		udp::socket m_socket;

		using receive_buffer = std::array<char, 1500>;
		std::unique_ptr<receive_buffer> m_buf;
		aux::listen_socket_handle m_listen_socket;

		std::uint16_t m_bind_port;

		aux::proxy_settings m_proxy_settings;

		std::shared_ptr<socks5> m_socks5_connection;

		bool m_abort:1;

#if TORRENT_USE_ASSERTS
		bool m_started;
		int m_magic;
#endif
	};
}

#endif
