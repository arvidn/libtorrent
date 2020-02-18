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

#ifndef TORRENT_BROADCAST_SOCKET_HPP_INCLUDED
#define TORRENT_BROADCAST_SOCKET_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"

#include <memory>
#include <list>
#include <array>

namespace libtorrent {

	// TODO: 2 factor these functions out
	TORRENT_EXTRA_EXPORT bool is_global(address const& a);
	TORRENT_EXTRA_EXPORT bool is_local(address const& a);
	TORRENT_EXTRA_EXPORT bool is_link_local(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_loopback(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_any(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_teredo(address const& addr);
	TORRENT_EXTRA_EXPORT bool is_ip_address(std::string const& host);

	// internal
	// TODO: refactor these out too
	template <typename Endpoint>
	bool is_v4(Endpoint const& ep)
	{
		return ep.protocol() == Endpoint::protocol_type::v4();
	}
	template <typename Endpoint>
	bool is_v6(Endpoint const& ep)
	{
		return ep.protocol() == Endpoint::protocol_type::v6();
	}

	// determines if the operating system supports IPv6
	TORRENT_EXTRA_EXPORT bool supports_ipv6();
	address ensure_v6(address const& a);

	using receive_handler_t = std::function<void(udp::endpoint const& from
		, span<char const> buffer)>;

	class TORRENT_EXTRA_EXPORT broadcast_socket
	{
	public:
		explicit broadcast_socket(udp::endpoint const& multicast_endpoint);
		~broadcast_socket() { close(); }

		void open(receive_handler_t handler, io_service& ios
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
					&& is_v4(socket->local_endpoint(ec));
			}
			address_v4 broadcast_address() const
			{
				error_code ec;
				return address_v4::broadcast(socket->local_endpoint(ec).address().to_v4(), netmask);
			}
		};

		void on_receive(socket_entry* s, error_code const& ec
			, std::size_t bytes_transferred);
		void open_unicast_socket(io_service& ios, address const& addr
			, address_v4 const& mask);
		void open_multicast_socket(io_service& ios, address const& addr
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
