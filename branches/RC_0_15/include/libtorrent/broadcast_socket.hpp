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

#ifndef TORRENT_BROADCAST_SOCKET_HPP_INCLUDED
#define TORRENT_BROADCAST_SOCKET_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp"
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <list>

namespace libtorrent
{

	TORRENT_EXPORT bool is_local(address const& a);
	TORRENT_EXPORT bool is_loopback(address const& addr);
	TORRENT_EXPORT bool is_multicast(address const& addr);
	TORRENT_EXPORT bool is_any(address const& addr);
	TORRENT_EXPORT int cidr_distance(address const& a1, address const& a2);

	// determines if the operating system supports IPv6
	TORRENT_EXPORT bool supports_ipv6();

	TORRENT_EXPORT int common_bits(unsigned char const* b1
		, unsigned char const* b2, int n);

	TORRENT_EXPORT address guess_local_address(io_service&);

	typedef boost::function<void(udp::endpoint const& from
		, char* buffer, int size)> receive_handler_t;

	class TORRENT_EXPORT broadcast_socket
	{
	public:
		broadcast_socket(io_service& ios, udp::endpoint const& multicast_endpoint
			, receive_handler_t const& handler, bool loopback = true);
		~broadcast_socket() { close(); }

		void send(char const* buffer, int size, error_code& ec);
		void close();
		int num_send_sockets() const { return m_unicast_sockets.size(); }
		void enable_ip_broadcast(bool e);

	private:

		struct socket_entry
		{
			socket_entry(boost::shared_ptr<datagram_socket> const& s)
				: socket(s) {}
			socket_entry(boost::shared_ptr<datagram_socket> const& s
				, address_v4 const& mask): socket(s), netmask(mask) {}
			boost::shared_ptr<datagram_socket> socket;
			char buffer[1024];
			udp::endpoint remote;
			address_v4 netmask;
			void close()
			{
				if (!socket) return;
				error_code ec;
				socket->close(ec);
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

		// if set, use IP broadcast as well as IP multicast
		// this is off by default because it's expensive in
		// terms of bandwidth usage
		bool m_ip_broadcast;
		
	};
}
	
#endif

