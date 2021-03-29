/*

Copyright (c) 2007-2013, 2015-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UDP_SOCKET_HPP_INCLUDED
#define TORRENT_UDP_SOCKET_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"

#include <array>
#include <memory>

namespace lt::aux {

	struct alert_manager;
	struct socks5;

	using udp_send_flags_t = flags::bitfield_flag<std::uint8_t, struct udp_send_flags_tag>;

	class TORRENT_EXTRA_EXPORT udp_socket : single_threaded
	{
	public:
		udp_socket(io_context& ios, aux::listen_socket_handle ls);

		// non-copyable
		udp_socket(udp_socket const&) = delete;
		udp_socket& operator=(udp_socket const&) = delete;

		static inline constexpr udp_send_flags_t peer_connection = 0_bit;
		static inline constexpr udp_send_flags_t tracker_connection = 1_bit;
		static inline constexpr udp_send_flags_t dont_queue = 2_bit;
		static inline constexpr udp_send_flags_t dont_fragment = 3_bit;

		bool is_open() const { return m_abort == false; }
		udp::socket::executor_type get_executor() { return m_socket.get_executor(); }

		template <typename Handler>
		void async_read(Handler&& h)
		{
			m_socket.async_wait(udp::socket::wait_read, std::forward<Handler>(h));
		}

		template <typename Handler>
		void async_write(Handler&& h)
		{
			m_socket.async_wait(udp::socket::wait_write, std::forward<Handler>(h));
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

		void set_proxy_settings(aux::proxy_settings const& ps, aux::alert_manager& alerts);
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

#ifdef TCP_NOTSENT_LOWAT
		void set_option(tcp_notsent_lowat const&, error_code&) {}
#endif

		template <class SocketOption>
		void get_option(SocketOption& opt, error_code& ec)
		{
			m_socket.get_option(opt, ec);
		}

		bool active_socks5() const;

	private:

		void wrap(udp::endpoint const& ep, span<char const> p, error_code& ec, udp_send_flags_t flags);
		void wrap(char const* hostname, int port, span<char const> p, error_code& ec, udp_send_flags_t flags);
		bool unwrap(udp::endpoint& from, span<char>& buf);

		udp::socket m_socket;

		io_context& m_ioc;

		using receive_buffer = std::array<char, 1500>;
		std::unique_ptr<receive_buffer> m_buf;
		aux::listen_socket_handle m_listen_socket;

		std::uint16_t m_bind_port;

		aux::proxy_settings m_proxy_settings;

		std::shared_ptr<socks5> m_socks5_connection;

		bool m_abort:1;
	};
}

#endif
