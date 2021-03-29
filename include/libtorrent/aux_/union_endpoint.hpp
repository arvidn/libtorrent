/*

Copyright (c) 2010, 2013, 2015-2020, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UNION_ENDPOINT_HPP_INCLUDED
#define TORRENT_UNION_ENDPOINT_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"

namespace lt::aux {

	struct union_address
	{
		union_address() { *this = address(); }
		explicit union_address(address const& a) { *this = a; }
		union_address& operator=(address const& a) &
		{
			v4 = a.is_v4();
			if (v4)
				addr.v4 = a.to_v4().to_bytes();
			else
				addr.v6 = a.to_v6().to_bytes();
			return *this;
		}

		bool operator==(union_address const& rh) const
		{
			if (v4 != rh.v4) return false;
			if (v4)
				return addr.v4 == rh.addr.v4;
			else
				return addr.v6 == rh.addr.v6;
		}

		bool operator!=(union_address const& rh) const
		{
			return !(*this == rh);
		}

		operator address() const
		{
			if (v4) return address(address_v4(addr.v4));
			else return address(address_v6(addr.v6));
		}

		union addr_t
		{
			address_v4::bytes_type v4;
			address_v6::bytes_type v6;
		};
		addr_t addr;
		bool v4:1;
	};

	struct union_endpoint
	{
		explicit union_endpoint(tcp::endpoint const& ep) { *this = ep; }
		explicit union_endpoint(udp::endpoint const& ep) { *this = ep; }
		union_endpoint() { *this = tcp::endpoint(); }

		union_endpoint& operator=(udp::endpoint const& ep) &
		{
			addr = ep.address();
			port = ep.port();
			return *this;
		}

		operator udp::endpoint() const { return udp::endpoint(addr, port); }

		union_endpoint& operator=(tcp::endpoint const& ep) &
		{
			addr = ep.address();
			port = ep.port();
			return *this;
		}

		lt::address address() const { return addr; }
		operator tcp::endpoint() const { return tcp::endpoint(addr, port); }

		union_address addr;
		std::uint16_t port;
	};
}

#endif
