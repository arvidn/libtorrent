/*

Copyright (c) 2016, Steven Siloti
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

#ifndef TORRENT_NETLINK_HPP
#define TORRENT_NETLINK_HPP

#include "libtorrent/config.hpp"

#if TORRENT_USE_NETLINK

#include <cstring>
#include <cstdint>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <boost/asio/basic_raw_socket.hpp>

namespace libtorrent
{
	template <typename Protocol>
	class basic_nl_endpoint
	{
	public:
		using protocol_type = Protocol;
		using data_type = boost::asio::detail::socket_addr_type;

		basic_nl_endpoint()
		{
			std::memset(&sockaddr, 0, sizeof(sockaddr_nl));
			sockaddr.nl_family = AF_NETLINK;
			sockaddr.nl_groups = 0;
			sockaddr.nl_pid = 0;
		}

		basic_nl_endpoint(protocol_type netlink_family, std::uint32_t group, ::pid_t pid = 0)
			: proto(netlink_family)
		{
			std::memset(&sockaddr, 0, sizeof(sockaddr_nl));
			sockaddr.nl_family = AF_NETLINK;
			sockaddr.nl_groups = group;
			sockaddr.nl_pid = pid;
		}

		basic_nl_endpoint(basic_nl_endpoint const& other)
		{
			sockaddr = other.sockaddr;
		}

		basic_nl_endpoint& operator=(const basic_nl_endpoint& other)
		{
			sockaddr = other.sockaddr;
			return *this;
		}

		basic_nl_endpoint& operator=(const basic_nl_endpoint&& other)
		{
			sockaddr = other.sockaddr;
			return *this;
		}

		protocol_type protocol() const
		{
			return proto;
		}

		data_type* data()
		{
			return &sockaddr;
		}

		const data_type* data() const
		{
			return (struct sockaddr*)&sockaddr;
		}

		std::size_t size() const
		{
			return sizeof(sockaddr);
		}

		std::size_t capacity() const
		{
			return sizeof(sockaddr);
		}

		friend bool operator==(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return l.sockaddr == r.sockaddr;
		}

		friend bool operator!=(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return !(l.sockaddr == r.sockaddr);
		}

		friend bool operator<(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return l.sockaddr < r.sockaddr;
		}

		friend bool operator>(const basic_nl_endpoint<Protocol>& l
			, const basic_nl_endpoint<Protocol>& r)
		{
			return r.sockaddr < l.sockaddr;
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

		private:
			protocol_type proto;
			sockaddr_nl sockaddr;
	};

	class netlink
	{
	public:
		using endpoint = basic_nl_endpoint<netlink>;
		using socket = boost::asio::basic_raw_socket<netlink>;

		netlink() : nl_family(0)
		{
		}

		explicit netlink(int nl_family)
			: nl_family(nl_family)
		{
		}

		int type() const
		{
			return SOCK_RAW;
		}

		int protocol() const
		{
			return nl_family;
		}

		int family() const
		{
			return AF_NETLINK;
		}

		friend bool operator==(const netlink& l, const netlink& r)
		{
			return l.nl_family == r.nl_family;
		}

		friend bool operator!=(const netlink& l, const netlink& r)
		{
			return l.nl_family != r.nl_family;
		}

	private:
		int nl_family;
	};

}

#endif

#endif
