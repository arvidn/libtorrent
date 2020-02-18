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

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/multicast.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/debug.hpp"

using namespace std::placeholders;

namespace libtorrent {

	bool is_ip_address(std::string const& host)
	{
		error_code ec;
		make_address(host, ec);
		return !ec;
	}

	bool is_global(address const& a)
	{
		if (a.is_v6())
		{
			// https://www.iana.org/assignments/ipv6-address-space/ipv6-address-space.xhtml
			address_v6 const a6 = a.to_v6();
			return (a6.to_bytes()[0] & 0xe0) == 0x20;
		}
		else
		{
			address_v4 const a4 = a.to_v4();
			return !(a4.is_multicast() || a4.is_unspecified() || is_local(a));
		}
	}

	bool is_link_local(address const& a)
	{
		if (a.is_v6())
		{
			address_v6 const a6 = a.to_v6();
			return a6.is_link_local()
				|| a6.is_multicast_link_local();
		}
		address_v4 const a4 = a.to_v4();
		unsigned long ip = a4.to_ulong();
		return (ip & 0xffff0000) == 0xa9fe0000; // 169.254.x.x
	}

	bool is_local(address const& a)
	{
		if (a.is_v6())
		{
			// NOTE: site local is deprecated but by
			// https://www.ietf.org/rfc/rfc3879.txt:
			// routers SHOULD be configured to prevent
			// routing of this prefix by default.

			address_v6 const a6 = a.to_v6();
			return a6.is_loopback()
				|| a6.is_link_local()
				|| a6.is_site_local()
				|| a6.is_multicast_link_local()
				|| a6.is_multicast_site_local()
				//  fc00::/7, unique local address
				|| (a6.to_bytes()[0] & 0xfe) == 0xfc;
		}
		address_v4 a4 = a.to_v4();
		unsigned long ip = a4.to_ulong();
		return ((ip & 0xff000000) == 0x0a000000 // 10.x.x.x
			|| (ip & 0xfff00000) == 0xac100000 // 172.16.x.x
			|| (ip & 0xffff0000) == 0xc0a80000 // 192.168.x.x
			|| (ip & 0xffff0000) == 0xa9fe0000 // 169.254.x.x
			|| (ip & 0xff000000) == 0x7f000000); // 127.x.x.x
	}

	// TODO: this function is pointless
	bool is_loopback(address const& addr)
	{
		return addr.is_loopback();
	}

	bool is_any(address const& addr)
	{
		if (addr.is_v4())
			return addr.to_v4() == address_v4::any();
		else if (addr.to_v6().is_v4_mapped())
			return (addr.to_v6().to_v4() == address_v4::any());
		else
			return addr.to_v6() == address_v6::any();
	}

	bool is_teredo(address const& addr)
	{
		if (!addr.is_v6()) return false;
		static const std::uint8_t teredo_prefix[] = {0x20, 0x01, 0, 0};
		address_v6::bytes_type b = addr.to_v6().to_bytes();
		return std::memcmp(b.data(), teredo_prefix, 4) == 0;
	}

	bool supports_ipv6()
	{
#if defined TORRENT_BUILD_SIMULATOR
		return true;
#elif defined TORRENT_WINDOWS
		TORRENT_TRY {
			error_code ec;
			make_address("::1", ec);
			return !ec;
		} TORRENT_CATCH(std::exception const&) { return false; }
#else
		io_service ios;
		tcp::socket test(ios);
		error_code ec;
		test.open(tcp::v6(), ec);
		if (ec) return false;
		error_code ignore;
		test.bind(tcp::endpoint(make_address_v6("::1", ignore), 0), ec);
		return !bool(ec);
#endif
	}

	address ensure_v6(address const& a)
	{
		return a == address_v4() ? address_v6() : a;
	}

	broadcast_socket::broadcast_socket(
		udp::endpoint const& multicast_endpoint)
		: m_multicast_endpoint(multicast_endpoint)
		, m_outstanding_operations(0)
		, m_abort(false)
	{
		TORRENT_ASSERT(m_multicast_endpoint.address().is_multicast());
	}

	void broadcast_socket::open(receive_handler_t handler
		, io_service& ios, error_code& ec, bool loopback)
	{
		m_on_receive = std::move(handler);

		std::vector<ip_interface> interfaces = enum_net_interfaces(ios, ec);

		if (is_v6(m_multicast_endpoint))
			open_multicast_socket(ios, address_v6::any(), loopback, ec);
		else
			open_multicast_socket(ios, address_v4::any(), loopback, ec);

		for (auto const& i : interfaces)
		{
			// only multicast on compatible networks
			if (i.interface_address.is_v4() != is_v4(m_multicast_endpoint)) continue;
			// ignore any loopback interface
			if (!loopback && is_loopback(i.interface_address)) continue;

			ec = error_code();

			open_multicast_socket(ios, i.interface_address, loopback, ec);
			open_unicast_socket(ios, i.interface_address
				, i.netmask.is_v4() ? i.netmask.to_v4() : address_v4());
		}
	}

	void broadcast_socket::open_multicast_socket(io_service& ios
		, address const& addr, bool loopback, error_code& ec)
	{
		using namespace boost::asio::ip::multicast;

		std::shared_ptr<udp::socket> s = std::make_shared<udp::socket>(ios);
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
		m_sockets.emplace_back(s);
		socket_entry& se = m_sockets.back();
		ADD_OUTSTANDING_ASYNC("broadcast_socket::on_receive");
		s->async_receive_from(boost::asio::buffer(se.buffer)
			, se.remote, std::bind(&broadcast_socket::on_receive, this, &se, _1, _2));
		++m_outstanding_operations;
	}

	void broadcast_socket::open_unicast_socket(io_service& ios, address const& addr
		, address_v4 const& mask)
	{
		error_code ec;
		std::shared_ptr<udp::socket> s = std::make_shared<udp::socket>(ios);
		s->open(addr.is_v4() ? udp::v4() : udp::v6(), ec);
		if (ec) return;

		m_unicast_sockets.emplace_back(s, mask);
		socket_entry& se = m_unicast_sockets.back();

		// allow sending broadcast messages
		boost::asio::socket_base::broadcast option(true);
		s->set_option(option, ec);
		if (!ec) se.broadcast = true;

		ADD_OUTSTANDING_ASYNC("broadcast_socket::on_receive");
		s->async_receive_from(boost::asio::buffer(se.buffer)
			, se.remote, std::bind(&broadcast_socket::on_receive, this, &se, _1, _2));
		++m_outstanding_operations;
	}

	void broadcast_socket::send_to(char const* buffer, int size
		, udp::endpoint const& to, error_code& ec)
	{
		bool all_fail = true;
		error_code e;
		for (auto& s : m_sockets)
		{
			if (!s.socket) continue;
			s.socket->send_to(boost::asio::buffer(buffer, std::size_t(size)), to, 0, e);
			if (e)
			{
				s.socket->close(e);
				s.socket.reset();
			}
			else
			{
				all_fail = false;
			}
		}
		if (all_fail) ec = e;
	}

	void broadcast_socket::send(char const* buffer, int const size
		, error_code& ec, int const flags)
	{
		bool all_fail = true;
		error_code e;

		for (auto& s : m_unicast_sockets)
		{
			if (!s.socket) continue;
			s.socket->send_to(boost::asio::buffer(buffer, std::size_t(size)), m_multicast_endpoint, 0, e);

			// if the user specified the broadcast flag, send one to the broadcast
			// address as well
			if ((flags & broadcast_socket::flag_broadcast) && s.can_broadcast())
				s.socket->send_to(boost::asio::buffer(buffer, std::size_t(size))
					, udp::endpoint(s.broadcast_address(), m_multicast_endpoint.port()), 0, e);

			if (e)
			{
				s.socket->close(e);
				s.socket.reset();
			}
			else
			{
				all_fail = false;
			}
		}

		for (auto& s : m_sockets)
		{
			if (!s.socket) continue;
			s.socket->send_to(boost::asio::buffer(buffer, std::size_t(size)), m_multicast_endpoint, 0, e);
			if (e)
			{
				s.socket->close(e);
				s.socket.reset();
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
		COMPLETE_ASYNC("broadcast_socket::on_receive");
		TORRENT_ASSERT(m_outstanding_operations > 0);
		--m_outstanding_operations;

		if (ec || bytes_transferred == 0 || !m_on_receive)
		{
			maybe_abort();
			return;
		}
		m_on_receive(s->remote, {s->buffer.data(), int(bytes_transferred)});

		if (maybe_abort()) return;
		if (!s->socket) return;
		ADD_OUTSTANDING_ASYNC("broadcast_socket::on_receive");
		s->socket->async_receive_from(boost::asio::buffer(s->buffer)
			, s->remote, std::bind(&broadcast_socket::on_receive, this, s, _1, _2));
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
		std::for_each(m_sockets.begin(), m_sockets.end(), std::bind(&socket_entry::close, _1));
		std::for_each(m_unicast_sockets.begin(), m_unicast_sockets.end(), std::bind(&socket_entry::close, _1));

		m_abort = true;
		maybe_abort();
	}
}
