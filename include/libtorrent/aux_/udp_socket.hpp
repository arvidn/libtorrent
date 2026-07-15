/*

Copyright (c) 2007-2013, 2015-2022, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
Copyright (c) 2016, Steven Siloti
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
#include "libtorrent/aux_/resolver_interface.hpp"

#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/aux_/proxy_base.hpp" // for aux::wrap_allocator
#include "libtorrent/time.hpp"
#include "libtorrent/error.hpp"
#include <winsock2.h> // for select(), FD_SET, TIMEVAL
#include <type_traits> // for std::decay_t
#endif

#include <array>
#include <memory>

namespace libtorrent::aux {

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
#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
			// boost.asio's Windows IOCP backend implements socket::async_wait()
			// (readiness notification without performing I/O) by falling back to
			// a select_reactor, running an extra background thread. Avoid that by
			// issuing a real overlapped receive and caching its result; read()
			// consumes it instead of calling receive_from() for the first packet.
			// aux::wrap_allocator (see proxy_base.hpp) forwards h's associated
			// allocator and executor, so this doesn't have to be a plain lambda.
			// m_recv_from is a persistent member, not a fresh local, and an
			// errored completion does not populate it; reset it so a failed
			// receive can't be attributed to whatever peer the previous,
			// unrelated successful receive happened to be from.
			m_recv_from = udp::endpoint();
			ADD_OUTSTANDING_ASYNC("udp_socket::async_read");
			m_socket.async_receive_from(boost::asio::buffer(*m_buf),
				m_recv_from,
				aux::wrap_allocator(
					[this, alive = std::weak_ptr<void>(m_alive_token)](
						error_code const& ec, std::size_t const bytes, std::decay_t<Handler> hn) {
						COMPLETE_ASYNC("udp_socket::async_read");
						bool const fatal =
							ec == error::operation_aborted || ec == error::bad_descriptor;
						// a cancelled receive's completion can still run after this
						// udp_socket has been destroyed (e.g. its listen socket was
						// removed while the receive was outstanding). In that case
						// alive.lock() fails and member state must not be touched.
						if (alive.lock())
						{
							m_recv_ec = ec;
							m_recv_bytes = bytes;
							m_recv_valid = true;
						}
						hn(fatal ? ec : error_code());
					},
					std::forward<Handler>(h)));
#else
			m_socket.async_wait(udp::socket::wait_read, std::forward<Handler>(h));
#endif
		}

		template <typename Handler>
		void async_write(Handler&& h)
		{
#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
			// there's no overlapped-I/O equivalent of "notify me when writable"
			// that doesn't involve a select_reactor (see async_read() above).
			// Approximate it with a retry timer, checked against a single,
			// immediate, non-blocking select() call each time it fires (see
			// poll_writable() / is_writable() below); unlike async_wait(), a
			// one-shot select() call doesn't run through boost.asio's reactor
			// and doesn't spin up a background thread.
			poll_writable(std::forward<Handler>(h));
#else
			m_socket.async_wait(udp::socket::wait_write, std::forward<Handler>(h));
#endif
		}

		struct packet
		{
			span<char> data;
			udp::endpoint from;
			string_view hostname;
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

		void set_proxy_settings(aux::proxy_settings const& ps, aux::alert_manager& alerts
			, aux::resolver_interface& resolver, bool send_local_ep);
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
		void wrap(
			char const* hostname,
			int port,
			span<char const> p,
			error_code& ec,
			udp_send_flags_t flags
		);

#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
		// see async_write(). Reschedules itself on m_write_retry_timer until
		// is_writable() reports the socket is actually writable, select()
		// itself fails, or the wait is cancelled, instead of unconditionally
		// waking the caller every time the timer fires. alive guards against
		// this udp_socket being destroyed while a retry is outstanding, the
		// same hazard async_read()'s completion handler guards against above.
		template <typename Handler>
		void poll_writable(Handler&& h)
		{
			m_write_retry_timer.expires_after(milliseconds(100));
			ADD_OUTSTANDING_ASYNC("udp_socket::poll_writable");
			m_write_retry_timer.async_wait(
				[this, alive = std::weak_ptr<void>(m_alive_token), h = std::forward<Handler>(h)](
					error_code const& ec) mutable {
					COMPLETE_ASYNC("udp_socket::poll_writable");
					if (ec || !alive.lock())
					{
						h(ec);
						return;
					}
					error_code select_ec;
					bool const writable = is_writable(select_ec);
					if (writable || select_ec)
					{
						h(select_ec);
						return;
					}
					poll_writable(std::move(h));
				});
		}

		// single, immediate, non-blocking query of this socket's writability.
		// Deliberately calls select() directly rather than going through
		// boost.asio, since asio's Windows async_wait() is what this whole
		// mechanism exists to avoid. Not const: native_handle() isn't. On
		// failure to query, sets ec and returns false.
		bool is_writable(error_code& ec)
		{
			fd_set write_fds;
			FD_ZERO(&write_fds);
			FD_SET(m_socket.native_handle(), &write_fds);
			TIMEVAL tv{0, 0};
			int const ret = ::select(0, nullptr, &write_fds, nullptr, &tv);
			if (ret == SOCKET_ERROR) ec = error_code(WSAGetLastError(), system_category());
			return ret > 0;
		}
#endif

		udp::socket m_socket;

		io_context& m_ioc;

		using receive_buffer = std::array<char, 1500>;
		std::unique_ptr<receive_buffer> m_buf;
		aux::listen_socket_handle m_listen_socket;

		std::uint16_t m_bind_port;

		aux::proxy_settings m_proxy_settings;

		std::shared_ptr<socks5> m_socks5_connection;

		bool m_abort:1;

#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
		// result of the last completed async_receive_from(), consumed by
		// read() in place of a synchronous receive_from() for the first
		// packet of a batch. See async_read().
		udp::endpoint m_recv_from;
		std::size_t m_recv_bytes = 0;
		error_code m_recv_ec;
		bool m_recv_valid = false;

		// used by async_write() to poll for the socket becoming writable
		// again, instead of waiting on a select_reactor. See async_write().
		aux::deadline_timer m_write_retry_timer;

		// weak_ptr'd from async_read()'s and async_write()'s completion
		// handlers, to detect whether this udp_socket has already been
		// destroyed by the time a cancelled operation's completion runs.
		// See async_read() and poll_writable().
		std::shared_ptr<void> m_alive_token = std::make_shared<char>(0);
#endif
	};

	// unwrap a SOCKS5-wrapped UDP datagram in-place. Returns false if the packet
	// is malformed or truncated and should be ignored. On success, ``pack.data``
	// is updated to point to the unwrapped payload, and either ``pack.from`` (for
	// IPv4/IPv6 destinations and resolvable hostnames) or ``pack.hostname`` (for
	// unresolvable hostnames) is populated.
	TORRENT_EXTRA_EXPORT bool socks5_unwrap(udp_socket::packet& pack);
}

#endif
