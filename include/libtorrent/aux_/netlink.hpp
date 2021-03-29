/*

Copyright (c) 2018, Eugene Shalygin
Copyright (c) 2016, Steven Siloti
Copyright (c) 2016-2017, 2021, Alden Torres
Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_NETLINK_HPP
#define TORRENT_NETLINK_HPP

#include "libtorrent/config.hpp"

#if TORRENT_USE_NETLINK

#include <cstring>
#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <boost/asio/basic_raw_socket.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

	template <typename Protocol>
	struct basic_nl_endpoint
	{
		using protocol_type = Protocol;
		using data_type = boost::asio::detail::socket_addr_type;

		basic_nl_endpoint() noexcept : basic_nl_endpoint(protocol_type(), 0, 0) {}

		basic_nl_endpoint(protocol_type netlink_family, std::uint32_t group, std::uint32_t pid = 0)
			: m_proto(netlink_family)
		{
			m_sockaddr.nl_family = AF_NETLINK;
			m_sockaddr.nl_groups = group;
			m_sockaddr.nl_pid = pid;
		}

		basic_nl_endpoint(basic_nl_endpoint const& other) = default;
		basic_nl_endpoint(basic_nl_endpoint&& other) noexcept = default;

		basic_nl_endpoint& operator=(basic_nl_endpoint const& other)
		{
			m_sockaddr = other.m_sockaddr;
			return *this;
		}

		basic_nl_endpoint& operator=(basic_nl_endpoint&& other) noexcept
		{
			m_sockaddr = other.m_sockaddr;
			return *this;
		}

		protocol_type protocol() const
		{
			return m_proto;
		}

		data_type* data() { return reinterpret_cast<data_type*>(&m_sockaddr); }

		const data_type* data() const
		{
			return reinterpret_cast<data_type const*>(&m_sockaddr);
		}

		std::size_t size() const { return sizeof(m_sockaddr); }
		std::size_t capacity() const { return sizeof(m_sockaddr); }

		// commented the comparison operators for now, until the
		// same operators are implemented for sockaddr_nl
		/*
		friend bool operator==(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return l.m_sockaddr == r.m_sockaddr;
		}

		friend bool operator!=(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return !(l.m_sockaddr == r.m_sockaddr);
		}

		friend bool operator<(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return l.m_sockaddr < r.m_sockaddr;
		}

		friend bool operator>(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return r.m_sockaddr < l.m_sockaddr;
		}

		friend bool operator<=(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return !(r < l);
		}

		friend bool operator>=(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return !(l < r);
		}
		*/

	private:
		protocol_type m_proto;
		sockaddr_nl m_sockaddr{};
	};

	struct netlink
	{
		using endpoint = basic_nl_endpoint<netlink>;
		using socket = boost::asio::basic_raw_socket<netlink>;

		netlink() = default;
		explicit netlink(int nl_family) : m_nl_family(nl_family) {}

		int type() const { return SOCK_RAW; }
		int protocol() const { return m_nl_family; }
		int family() const { return AF_NETLINK; }

		friend bool operator==(const netlink& l, const netlink& r)
		{
			return l.m_nl_family == r.m_nl_family;
		}

		friend bool operator!=(const netlink& l, const netlink& r)
		{
			return l.m_nl_family != r.m_nl_family;
		}

	private:
		int m_nl_family = NETLINK_ROUTE;
	};

}

#endif // TORRENT_USE_NETLINK

#endif
