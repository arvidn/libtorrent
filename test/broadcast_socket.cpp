/*

Copyright (c) 2015-2017, Steven Siloti
Copyright (c) 2007-2012, 2014-2020, Arvid Norberg
Copyright (c) 2016-2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/multicast.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/enum_net.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"

#include "broadcast_socket.hpp"

using namespace std::placeholders;

namespace lt {

	broadcast_socket::broadcast_socket(
		udp::endpoint multicast_endpoint)
		: m_multicast_endpoint(std::move(multicast_endpoint))
		, m_outstanding_operations(0)
		, m_abort(false)
	{
		TORRENT_ASSERT(m_multicast_endpoint.address().is_multicast());
	}

	void broadcast_socket::open(receive_handler_t handler
		, io_context& ios, error_code& ec, bool loopback)
	{
		m_on_receive = std::move(handler);

		std::vector<aux::ip_interface> interfaces = aux::enum_net_interfaces(ios, ec);

		if (aux::is_v6(m_multicast_endpoint))
			open_multicast_socket(ios, address_v6::any(), loopback, ec);
		else
			open_multicast_socket(ios, address_v4::any(), loopback, ec);

		for (auto const& i : interfaces)
		{
			// only multicast on compatible networks
			if (i.interface_address.is_v4() != aux::is_v4(m_multicast_endpoint)) continue;
			// ignore any loopback interface
			if (!loopback && i.interface_address.is_loopback()) continue;

			ec = error_code();

			open_multicast_socket(ios, i.interface_address, loopback, ec);
			open_unicast_socket(ios, i.interface_address
				, i.netmask.is_v4() ? i.netmask.to_v4() : address_v4());
		}
	}

	void broadcast_socket::open_multicast_socket(io_context& ios
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

	void broadcast_socket::open_unicast_socket(io_context& ios, address const& addr
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
