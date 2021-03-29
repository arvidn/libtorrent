/*

Copyright (c) 2016, Steven Siloti
Copyright (c) 2007-2012, 2015-2020, Arvid Norberg
Copyright (c) 2016-2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BROADCAST_SOCKET_HPP_INCLUDED
#define TORRENT_BROADCAST_SOCKET_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"
#include "test.hpp"

#include <memory>
#include <list>
#include <array>

namespace lt {

	using receive_handler_t = std::function<void(udp::endpoint const& from
		, span<char const> buffer)>;

	class EXPORT broadcast_socket
	{
	public:
		explicit broadcast_socket(udp::endpoint multicast_endpoint);
		~broadcast_socket() { close(); }

		void open(receive_handler_t handler, io_context& ios
			, error_code& ec, bool loopback = true);

		enum flags_t { flag_broadcast = 1 };
		void send(char const* buffer, int size, error_code& ec, int flags = 0);
		void send_to(char const* buffer, int size, udp::endpoint const& to, error_code& ec);

		void close();
		int num_send_sockets() const { return int(m_unicast_sockets.size()); }

	private:

		struct socket_entry
		{
			explicit socket_entry(std::shared_ptr<udp::socket> s)
				: socket(std::move(s)), broadcast(false) {}
			socket_entry(std::shared_ptr<udp::socket> s
				, address_v4 const& mask): socket(std::move(s)), netmask(mask), broadcast(false)
			{}
			std::shared_ptr<udp::socket> socket;
			std::array<char, 1500> buffer{};
			udp::endpoint remote;
			address_v4 netmask;
			bool broadcast;
			void close()
			{
				if (!socket) return;
				error_code ec;
				socket->close(ec);
			}
			bool can_broadcast() const
			{
				error_code ec;
				return broadcast
					&& netmask != address_v4()
					&& aux::is_v4(socket->local_endpoint(ec));
			}
			address_v4 broadcast_address() const
			{
				error_code ec;
				return make_network_v4(socket->local_endpoint(ec).address().to_v4(), netmask).broadcast();
			}
		};

		void on_receive(socket_entry* s, error_code const& ec
			, std::size_t bytes_transferred);
		void open_unicast_socket(io_context& ios, address const& addr
			, address_v4 const& mask);
		void open_multicast_socket(io_context& ios, address const& addr
			, bool loopback, error_code& ec);

		// if we're aborting, destruct the handler and return true
		bool maybe_abort();

		// these sockets are used to
		// join the multicast group (on each interface)
		// and receive multicast messages
		std::list<socket_entry> m_sockets;
		// these sockets are not bound to any
		// specific port and are used to
		// send messages to the multicast group
		// and receive unicast responses
		std::list<socket_entry> m_unicast_sockets;
		udp::endpoint m_multicast_endpoint;
		receive_handler_t m_on_receive;

		// the number of outstanding async operations
		// we have on these sockets. The m_on_receive
		// handler may not be destructed until this reaches
		// 0, since it may be holding references to
		// the broadcast_socket itself.
		int m_outstanding_operations;
		// when set to true, we're trying to shut down
		// don't initiate new operations and once the
		// outstanding counter reaches 0, destruct
		// the handler object
		bool m_abort;
	};
}

#endif
