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

#include <boost/version.hpp>

#ifndef NDEBUG
//#include "libtorrent/socket_io.hpp"
#endif

#include "libtorrent/config.hpp"

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#endif

#include <boost/bind.hpp>

#include "libtorrent/socket.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	bool is_local(address const& a)
	{
#if TORRENT_USE_IPV6
		if (a.is_v6()) return a.to_v6().is_link_local();
#endif
		address_v4 a4 = a.to_v4();
		unsigned long ip = a4.to_ulong();
		return ((ip & 0xff000000) == 0x0a000000 // 10.x.x.x
			|| (ip & 0xfff00000) == 0xac100000 // 172.16.x.x
			|| (ip & 0xffff0000) == 0xc0a80000 // 192.168.x.x
			|| (ip & 0xffff0000) == 0xa9fe0000); // 169.254.x.x
	}

	bool is_loopback(address const& addr)
	{
#if TORRENT_USE_IPV6
		if (addr.is_v4())
			return addr.to_v4() == address_v4::loopback();
		else
			return addr.to_v6() == address_v6::loopback();
#else
		return addr.to_v4() == address_v4::loopback();
#endif
	}

	bool is_multicast(address const& addr)
	{
#if TORRENT_USE_IPV6
		if (addr.is_v4())
			return addr.to_v4().is_multicast();
		else
			return addr.to_v6().is_multicast();
#else
		return addr.to_v4().is_multicast();
#endif
	}

	bool is_any(address const& addr)
	{
#if TORRENT_USE_IPV6
		if (addr.is_v4())
			return addr.to_v4() == address_v4::any();
		else if (addr.to_v6().is_v4_mapped())
			return (addr.to_v6().to_v4() == address_v4::any());
		else
			return addr.to_v6() == address_v6::any();
#else
		return addr.to_v4() == address_v4::any();
#endif
	}

	bool supports_ipv6()
	{
#if TORRENT_USE_IPV6
		error_code ec;
		address::from_string("::1", ec);
		return !ec;
#else
		return false;
#endif
	}

	address guess_local_address(io_service& ios)
	{
		// make a best guess of the interface we're using and its IP
		error_code ec;
		std::vector<ip_interface> const& interfaces = enum_net_interfaces(ios, ec);
		address ret = address_v4::any();
		for (std::vector<ip_interface>::const_iterator i = interfaces.begin()
			, end(interfaces.end()); i != end; ++i)
		{
			address const& a = i->interface_address;
			if (is_loopback(a)
				|| is_multicast(a)
				|| is_any(a)) continue;

			// prefer a v4 address, but return a v6 if
			// there are no v4
			if (a.is_v4()) return a;

			if (ret != address_v4::any())
				ret = a;
		}
		return ret;
	}

	// count the length of the common bit prefix
	int common_bits(unsigned char const* b1
		, unsigned char const* b2, int n)
	{
		for (int i = 0; i < n; ++i, ++b1, ++b2)
		{
			unsigned char a = *b1 ^ *b2;
			if (a == 0) continue;
			int ret = i * 8 + 8;
			for (; a > 0; a >>= 1) --ret;
			return ret;
		}
		return n * 8;
	}

	// returns the number of bits in that differ from the right
	// between the addresses.
	int cidr_distance(address const& a1, address const& a2)
	{
#if TORRENT_USE_IPV6
		if (a1.is_v4() && a2.is_v4())
		{
#endif
			// both are v4
			address_v4::bytes_type b1 = a1.to_v4().to_bytes();
			address_v4::bytes_type b2 = a2.to_v4().to_bytes();
			return address_v4::bytes_type::static_size * 8
				- common_bits(b1.c_array(), b2.c_array(), b1.size());
#if TORRENT_USE_IPV6
		}
	
		address_v6::bytes_type b1;
		address_v6::bytes_type b2;
		if (a1.is_v4()) b1 = address_v6::v4_mapped(a1.to_v4()).to_bytes();
		else b1 = a1.to_v6().to_bytes();
		if (a2.is_v4()) b2 = address_v6::v4_mapped(a2.to_v4()).to_bytes();
		else b2 = a2.to_v6().to_bytes();
		return address_v6::bytes_type::static_size * 8
			- common_bits(b1.c_array(), b2.c_array(), b1.size());
#endif
	}

	broadcast_socket::broadcast_socket(io_service& ios
		, udp::endpoint const& multicast_endpoint
		, receive_handler_t const& handler
		, bool loopback)
		: m_multicast_endpoint(multicast_endpoint)
		, m_on_receive(handler)
		, m_ip_broadcast(false)
	{
		TORRENT_ASSERT(is_multicast(m_multicast_endpoint.address()));

		using namespace asio::ip::multicast;
	
		error_code ec;
		std::vector<ip_interface> interfaces = enum_net_interfaces(ios, ec);

#if TORRENT_USE_IPV6
		if (multicast_endpoint.address().is_v6())
			open_multicast_socket(ios, address_v6::any(), loopback, ec);
		else
#endif
			open_multicast_socket(ios, address_v4::any(), loopback, ec);
		
		for (std::vector<ip_interface>::const_iterator i = interfaces.begin()
			, end(interfaces.end()); i != end; ++i)
		{
			// only multicast on compatible networks
			if (i->interface_address.is_v4() != multicast_endpoint.address().is_v4()) continue;
			// ignore any loopback interface
			if (!loopback && is_loopback(i->interface_address)) continue;

			ec = error_code();
			open_multicast_socket(ios, i->interface_address, loopback, ec);
#ifndef NDEBUG
//			fprintf(stderr, "broadcast socket [ if: %s group: %s ] %s\n"
//				, i->interface_address.to_string().c_str()
//				, print_address(multicast_endpoint.address()).c_str()
//				, ec.message().c_str());
#endif
			open_unicast_socket(ios, i->interface_address
				, i->netmask.is_v4() ? i->netmask.to_v4() : address_v4());
		}
	}

	void broadcast_socket::enable_ip_broadcast(bool e)
	{
		if (e == m_ip_broadcast) return;
		m_ip_broadcast = e;

		asio::socket_base::broadcast option(m_ip_broadcast);
		error_code ec;
		for (std::list<socket_entry>::iterator i = m_unicast_sockets.begin()
			, end(m_unicast_sockets.end()); i != end; ++i)
		{
			if (i->socket) continue;
			i->socket->set_option(option, ec);
		}
	}

	void broadcast_socket::open_multicast_socket(io_service& ios
		, address const& addr, bool loopback, error_code& ec)
	{
		using namespace asio::ip::multicast;

		boost::shared_ptr<datagram_socket> s(new datagram_socket(ios));
		s->open(addr.is_v4() ? udp::v4() : udp::v6(), ec);
		if (ec) return;
		s->set_option(datagram_socket::reuse_address(true), ec);
		if (ec) return;
		s->bind(udp::endpoint(addr, m_multicast_endpoint.port()), ec);
		if (ec) return;
		s->set_option(join_group(m_multicast_endpoint.address()), ec);
		if (ec) return;
		s->set_option(hops(255), ec);
		if (ec) return;
		s->set_option(enable_loopback(loopback), ec);
		if (ec) return;
		m_sockets.push_back(socket_entry(s));
		socket_entry& se = m_sockets.back();
		s->async_receive_from(asio::buffer(se.buffer, sizeof(se.buffer))
			, se.remote, boost::bind(&broadcast_socket::on_receive, this, &se, _1, _2));
	}

	void broadcast_socket::open_unicast_socket(io_service& ios, address const& addr
		, address_v4 const& mask)
	{
		using namespace asio::ip::multicast;
		error_code ec;
		boost::shared_ptr<datagram_socket> s(new datagram_socket(ios));
		s->open(addr.is_v4() ? udp::v4() : udp::v6(), ec);
		if (ec) return;
		s->bind(udp::endpoint(addr, 0), ec);
		if (ec) return;
		m_unicast_sockets.push_back(socket_entry(s, mask));
		socket_entry& se = m_unicast_sockets.back();
		s->async_receive_from(asio::buffer(se.buffer, sizeof(se.buffer))
			, se.remote, boost::bind(&broadcast_socket::on_receive, this, &se, _1, _2));
	}

	void broadcast_socket::send(char const* buffer, int size, error_code& ec)
	{
		for (std::list<socket_entry>::iterator i = m_unicast_sockets.begin()
			, end(m_unicast_sockets.end()); i != end; ++i)
		{
			if (!i->socket) continue;
			error_code e;
			i->socket->send_to(asio::buffer(buffer, size), m_multicast_endpoint, 0, e);
#ifndef NDEBUG
//			fprintf(stderr, " sending on unicast %s to: %s\n", print_address(i->socket->local_endpoint().address()).c_str()
//				, print_endpoint(m_multicast_endpoint).c_str());
#endif
			if (e)
			{
#ifndef NDEBUG
//				fprintf(stderr, " ERROR: %s\n", e.message().c_str());
#endif
				i->socket->close(e);
				i->socket.reset();
			}
		}

		for (std::list<socket_entry>::iterator i = m_sockets.begin()
			, end(m_sockets.end()); i != end; ++i)
		{
			if (!i->socket) continue;
			error_code e;
			i->socket->send_to(asio::buffer(buffer, size), m_multicast_endpoint, 0, e);
			if (m_ip_broadcast && i->socket->local_endpoint(e).address().is_v4())
				i->socket->send_to(asio::buffer(buffer, size)
					, udp::endpoint(i->broadcast_address(), m_multicast_endpoint.port()), 0, e);
#ifndef NDEBUG
//			extern std::string print_address(address const& addr);
//			extern std::string print_endpoint(udp::endpoint const& ep);
//			fprintf(stderr, " sending on multicast %s to: %s\n", print_address(i->socket->local_endpoint().address()).c_str()
//				, print_endpoint(m_multicast_endpoint).c_str());
#endif
			if (e)
			{
#ifndef NDEBUG
//				fprintf(stderr, " ERROR: %s\n", e.message().c_str());
#endif
				i->socket->close(e);
				i->socket.reset();
			}
		}
	}

	void broadcast_socket::on_receive(socket_entry* s, error_code const& ec
		, std::size_t bytes_transferred)
	{
		if (ec || bytes_transferred == 0 || !m_on_receive) return;
		m_on_receive(s->remote, s->buffer, bytes_transferred);
		if (!s->socket) return;
		s->socket->async_receive_from(asio::buffer(s->buffer, sizeof(s->buffer))
			, s->remote, boost::bind(&broadcast_socket::on_receive, this, s, _1, _2));
	}

	void broadcast_socket::close()
	{
		std::for_each(m_sockets.begin(), m_sockets.end(), boost::bind(&socket_entry::close, _1));
		std::for_each(m_unicast_sockets.begin(), m_unicast_sockets.end(), boost::bind(&socket_entry::close, _1));

		m_on_receive.clear();
	}
}


