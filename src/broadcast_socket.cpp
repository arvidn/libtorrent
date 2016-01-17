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

#include <boost/version.hpp>

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if defined TORRENT_OS2
#include <pthread.h>
#endif

#include <boost/bind.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>

#ifdef TORRENT_WINDOWS
#include <iphlpapi.h> // for if_nametoindex
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/assert.hpp"

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#ifdef TORRENT_DEBUG
#include "libtorrent/socket_io.hpp"
#endif

namespace libtorrent
{
	bool is_local(address const& a)
	{
		TORRENT_TRY {
#if TORRENT_USE_IPV6
			if (a.is_v6())
			{
				return a.to_v6().is_loopback()
					|| a.to_v6().is_link_local()
					|| a.to_v6().is_multicast_link_local();
			}
#endif
			address_v4 a4 = a.to_v4();
			unsigned long ip = a4.to_ulong();
			return ((ip & 0xff000000) == 0x0a000000 // 10.x.x.x
				|| (ip & 0xfff00000) == 0xac100000 // 172.16.x.x
				|| (ip & 0xffff0000) == 0xc0a80000 // 192.168.x.x
				|| (ip & 0xffff0000) == 0xa9fe0000 // 169.254.x.x
				|| (ip & 0xff000000) == 0x7f000000); // 127.x.x.x
		} TORRENT_CATCH(std::exception&) { return false; }
	}

	bool is_loopback(address const& addr)
	{
#if TORRENT_USE_IPV6
		TORRENT_TRY {
			if (addr.is_v4())
				return addr.to_v4() == address_v4::loopback();
			else
				return addr.to_v6() == address_v6::loopback();
		} TORRENT_CATCH(std::exception&) { return false; }
#else
		return addr.to_v4() == address_v4::loopback();
#endif
	}

	bool is_multicast(address const& addr)
	{
#if TORRENT_USE_IPV6
		TORRENT_TRY {
			if (addr.is_v4())
				return addr.to_v4().is_multicast();
			else
				return addr.to_v6().is_multicast();
		} TORRENT_CATCH(std::exception&) { return false; }
#else
		return addr.to_v4().is_multicast();
#endif
	}

	bool is_any(address const& addr)
	{
		TORRENT_TRY {
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
		} TORRENT_CATCH(std::exception&) { return false; }
	}

	bool is_teredo(address const& addr)
	{
#if TORRENT_USE_IPV6
		TORRENT_TRY {
			if (!addr.is_v6()) return false;
			boost::uint8_t teredo_prefix[] = {0x20, 0x01, 0, 0};
			address_v6::bytes_type b = addr.to_v6().to_bytes();
			return memcmp(&b[0], teredo_prefix, 4) == 0;
		} TORRENT_CATCH(std::exception&) { return false; }
#else
		TORRENT_UNUSED(addr);
		return false;
#endif
	}

	bool supports_ipv6()
	{
#if !TORRENT_USE_IPV6
		return false;
#elif defined TORRENT_BUILD_SIMULATOR
		return true;
#elif defined TORRENT_WINDOWS
		TORRENT_TRY {
			error_code ec;
			address::from_string("::1", ec);
			return !ec;
		} TORRENT_CATCH(std::exception&) { return false; }
#else
		io_service ios;
		tcp::socket test(ios);
		error_code ec;
		test.open(tcp::v6(), ec);
		return !bool(ec);
#endif
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
	// between the addresses. The larger number, the further apart
	// the IPs are
	int cidr_distance(address const& a1, address const& a2)
	{
#if TORRENT_USE_IPV6
		if (a1.is_v4() && a2.is_v4())
		{
#endif
			// both are v4
			address_v4::bytes_type b1 = a1.to_v4().to_bytes();
			address_v4::bytes_type b2 = a2.to_v4().to_bytes();
			return address_v4::bytes_type().size() * 8
				- common_bits(b1.data(), b2.data(), b1.size());
#if TORRENT_USE_IPV6
		}

		address_v6::bytes_type b1;
		address_v6::bytes_type b2;
		if (a1.is_v4()) b1 = address_v6::v4_mapped(a1.to_v4()).to_bytes();
		else b1 = a1.to_v6().to_bytes();
		if (a2.is_v4()) b2 = address_v6::v4_mapped(a2.to_v4()).to_bytes();
		else b2 = a2.to_v6().to_bytes();
		return address_v6::bytes_type().size() * 8
			- common_bits(b1.data(), b2.data(), b1.size());
#endif
	}

	broadcast_socket::broadcast_socket(
		udp::endpoint const& multicast_endpoint)
		: m_multicast_endpoint(multicast_endpoint)
		, m_outstanding_operations(0)
		, m_abort(false)
	{
		TORRENT_ASSERT(is_multicast(m_multicast_endpoint.address()));

		using namespace boost::asio::ip::multicast;
	}

	void broadcast_socket::open(receive_handler_t const& handler
		, io_service& ios, error_code& ec, bool loopback)
	{
		m_on_receive = handler;

		std::vector<ip_interface> interfaces = enum_net_interfaces(ios, ec);

#if TORRENT_USE_IPV6
		if (m_multicast_endpoint.address().is_v6())
			open_multicast_socket(ios, address_v6::any(), loopback, ec);
		else
#endif
			open_multicast_socket(ios, address_v4::any(), loopback, ec);

		for (std::vector<ip_interface>::const_iterator i = interfaces.begin()
			, end(interfaces.end()); i != end; ++i)
		{
			// only multicast on compatible networks
			if (i->interface_address.is_v4() != m_multicast_endpoint.address().is_v4()) continue;
			// ignore any loopback interface
			if (!loopback && is_loopback(i->interface_address)) continue;

			ec = error_code();

			// if_nametoindex was introduced in vista
#if TORRENT_USE_IPV6 \
		&& (!defined TORRENT_WINDOWS || _WIN32_WINNT >= 0x0600) \
		&& !defined TORRENT_MINGW

			if (i->interface_address.is_v6() &&
				i->interface_address.to_v6().is_link_local())
			{
				address_v6 addr6 = i->interface_address.to_v6();
				addr6.scope_id(if_nametoindex(i->name));
				open_multicast_socket(ios, addr6, loopback, ec);

				address_v4 const& mask = i->netmask.is_v4()
					? i->netmask.to_v4() : address_v4();
				open_unicast_socket(ios, addr6, mask);
				continue;
			}

#endif
			open_multicast_socket(ios, i->interface_address, loopback, ec);
#ifdef TORRENT_DEBUG
			fprintf(stderr, "broadcast socket [ if: %s group: %s mask: %s ] %s\n"
				, i->interface_address.to_string().c_str()
				, m_multicast_endpoint.address().to_string().c_str()
				, i->netmask.to_string().c_str()
				, ec.message().c_str());
#endif
			open_unicast_socket(ios, i->interface_address
				, i->netmask.is_v4() ? i->netmask.to_v4() : address_v4());
		}
	}

	void broadcast_socket::open_multicast_socket(io_service& ios
		, address const& addr, bool loopback, error_code& ec)
	{
		using namespace boost::asio::ip::multicast;

		boost::shared_ptr<udp::socket> s(new udp::socket(ios));
		s->open(addr.is_v4() ? udp::v4() : udp::v6(), ec);
		if (ec) return;
		s->set_option(udp::socket::reuse_address(true), ec);
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
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("broadcast_socket::on_receive");
#endif
		s->async_receive_from(boost::asio::buffer(se.buffer, sizeof(se.buffer))
			, se.remote, boost::bind(&broadcast_socket::on_receive, this, &se, _1, _2));
		++m_outstanding_operations;
	}

	void broadcast_socket::open_unicast_socket(io_service& ios, address const& addr
		, address_v4 const& mask)
	{
		using namespace boost::asio::ip::multicast;
		error_code ec;
		boost::shared_ptr<udp::socket> s(new udp::socket(ios));
		s->open(addr.is_v4() ? udp::v4() : udp::v6(), ec);
		if (ec) return;
		s->bind(udp::endpoint(addr, 0), ec);
		if (ec) return;

		m_unicast_sockets.push_back(socket_entry(s, mask));
		socket_entry& se = m_unicast_sockets.back();

		// allow sending broadcast messages
		boost::asio::socket_base::broadcast option(true);
		s->set_option(option, ec);
		if (!ec) se.broadcast = true;

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("broadcast_socket::on_receive");
#endif
		s->async_receive_from(boost::asio::buffer(se.buffer, sizeof(se.buffer))
			, se.remote, boost::bind(&broadcast_socket::on_receive, this, &se, _1, _2));
		++m_outstanding_operations;
	}

	void broadcast_socket::send(char const* buffer, int size, error_code& ec, int flags)
	{
		bool all_fail = true;
		error_code e;

		for (std::list<socket_entry>::iterator i = m_unicast_sockets.begin()
			, end(m_unicast_sockets.end()); i != end; ++i)
		{
			if (!i->socket) continue;
			i->socket->send_to(boost::asio::buffer(buffer, size), m_multicast_endpoint, 0, e);

			// if the user specified the broadcast flag, send one to the broadcast
			// address as well
			if ((flags & broadcast_socket::broadcast) && i->can_broadcast())
				i->socket->send_to(boost::asio::buffer(buffer, size)
					, udp::endpoint(i->broadcast_address(), m_multicast_endpoint.port()), 0, e);

			if (e)
			{
				i->socket->close(e);
				i->socket.reset();
			}
			else
			{
				all_fail = false;
			}
		}

		for (std::list<socket_entry>::iterator i = m_sockets.begin()
			, end(m_sockets.end()); i != end; ++i)
		{
			if (!i->socket) continue;
			i->socket->send_to(boost::asio::buffer(buffer, size), m_multicast_endpoint, 0, e);
			if (e)
			{
				i->socket->close(e);
				i->socket.reset();
			}
			else
			{
				all_fail = false;
			}
		}

		if (all_fail) ec = e;
	}

	void broadcast_socket::on_receive(socket_entry* s, error_code const& ec
		, std::size_t bytes_transferred)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("broadcast_socket::on_receive");
#endif
		TORRENT_ASSERT(m_outstanding_operations > 0);
		--m_outstanding_operations;

		if (ec || bytes_transferred == 0 || !m_on_receive)
		{
			maybe_abort();
			return;
		}
		m_on_receive(s->remote, s->buffer, bytes_transferred);

		if (maybe_abort()) return;
		if (!s->socket) return;
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("broadcast_socket::on_receive");
#endif
		s->socket->async_receive_from(boost::asio::buffer(s->buffer, sizeof(s->buffer))
			, s->remote, boost::bind(&broadcast_socket::on_receive, this, s, _1, _2));
		++m_outstanding_operations;
	}

	bool broadcast_socket::maybe_abort()
	{
		bool ret = m_abort;
		if (m_abort && m_outstanding_operations == 0)
		{
			// it's important that m_on_receive is cleared
			// before the object is destructed, since it may
			// hold a reference to ourself, which would otherwise
			// cause an infinite recursion destructing the objects
			receive_handler_t().swap(m_on_receive);
		}
		return ret;
	}

	void broadcast_socket::close()
	{
		std::for_each(m_sockets.begin(), m_sockets.end(), boost::bind(&socket_entry::close, _1));
		std::for_each(m_unicast_sockets.begin(), m_unicast_sockets.end(), boost::bind(&socket_entry::close, _1));

		m_abort = true;
		maybe_abort();
	}
}


