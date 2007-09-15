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

#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#include <boost/bind.hpp>

#include "libtorrent/socket.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	bool is_local(address const& a)
	{
		if (a.is_v6()) return false;
		address_v4 a4 = a.to_v4();
		unsigned long ip = a4.to_ulong();
		return ((ip & 0xff000000) == 0x0a000000
			|| (ip & 0xfff00000) == 0xac100000
			|| (ip & 0xffff0000) == 0xc0a80000);
	}

	address_v4 guess_local_address(asio::io_service& ios)
	{
		// make a best guess of the interface we're using and its IP
		udp::resolver r(ios);
		udp::resolver::iterator i = r.resolve(udp::resolver::query(asio::ip::host_name(), "0"));
		for (;i != udp::resolver_iterator(); ++i)
		{
			// ignore the loopback
			if (i->endpoint().address() == address_v4((127 << 24) + 1)) continue;
			// ignore non-IPv4 addresses
			if (i->endpoint().address().is_v4()) break;
		}
		if (i == udp::resolver_iterator()) return address_v4::any();
		return i->endpoint().address().to_v4();
	}

	broadcast_socket::broadcast_socket(asio::io_service& ios
		, udp::endpoint const& multicast_endpoint
		, receive_handler_t const& handler)
		: m_multicast_endpoint(multicast_endpoint)
		, m_on_receive(handler)
	{
		assert(m_multicast_endpoint.address().is_v4());
		assert(m_multicast_endpoint.address().to_v4().is_multicast());

		using namespace asio::ip::multicast;
	
		asio::error_code ec;
		std::vector<address> interfaces = enum_net_interfaces(ios, ec);

		for (std::vector<address>::const_iterator i = interfaces.begin()
			, end(interfaces.end()); i != end; ++i)
		{
			// only broadcast to IPv4 addresses that are not local
			if (!i->is_v4() || !is_local(*i)) continue;
			// ignore the loopback interface
			if (i->to_v4() == address_v4((127 << 24) + 1)) continue;

			boost::shared_ptr<datagram_socket> s(new datagram_socket(ios));
			s->open(udp::v4(), ec);
			if (ec) continue;
			s->set_option(datagram_socket::reuse_address(true), ec);
			if (ec) continue;
			s->bind(udp::endpoint(address_v4::any(), multicast_endpoint.port()), ec);
			if (ec) continue;
			s->set_option(join_group(multicast_endpoint.address()), ec);
			if (ec) continue;
			s->set_option(outbound_interface(i->to_v4()), ec);
			if (ec) continue;
			s->set_option(hops(255), ec);
			if (ec) continue;
			s->set_option(enable_loopback(true), ec);
			if (ec) continue;
			m_sockets.push_back(socket_entry(s));
			socket_entry& se = m_sockets.back();
			s->async_receive_from(asio::buffer(se.buffer, sizeof(se.buffer))
				, se.remote, bind(&broadcast_socket::on_receive, this, &se, _1, _2));
#ifndef NDEBUG
//			std::cerr << "broadcast socket [ if: " << i->to_v4().to_string()
//				<< " group: " << multicast_endpoint.address() << " ]" << std::endl;
#endif
		}
	}

	void broadcast_socket::send(char const* buffer, int size, asio::error_code& ec)
	{
		for (std::list<socket_entry>::iterator i = m_sockets.begin()
			, end(m_sockets.end()); i != end; ++i)
		{
			asio::error_code e;
			i->socket->send_to(asio::buffer(buffer, size), m_multicast_endpoint, 0, e);
#ifndef NDEBUG
//			std::cerr << " sending on " << i->socket->local_endpoint().address().to_string() << std::endl;
#endif
			if (e) ec = e;
		}
	}

	void broadcast_socket::on_receive(socket_entry* s, asio::error_code const& ec
		, std::size_t bytes_transferred)
	{
		if (ec || bytes_transferred == 0) return;
		m_on_receive(s->remote, s->buffer, bytes_transferred);
		s->socket->async_receive_from(asio::buffer(s->buffer, sizeof(s->buffer))
			, s->remote, bind(&broadcast_socket::on_receive, this, s, _1, _2));
	}

	void broadcast_socket::close()
	{
		for (std::list<socket_entry>::iterator i = m_sockets.begin()
			, end(m_sockets.end()); i != end; ++i)
		{
			i->socket->close();
		}
	}
}


