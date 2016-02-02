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

#ifndef TORRENT_ENUM_NET_HPP_INCLUDED
#define TORRENT_ENUM_NET_HPP_INCLUDED

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>

#if TORRENT_USE_IFCONF || TORRENT_USE_NETLINK || TORRENT_USE_SYSCTL
#include <sys/socket.h> // for SO_BINDTODEVICE
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"

namespace libtorrent
{
	struct socket_type;

	// the interface should not have a netmask
	struct ip_interface
	{
		address interface_address;
		address netmask;
		char name[64];
		int mtu;
	};

	struct ip_route
	{
		address destination;
		address netmask;
		address gateway;
		char name[64];
		int mtu;
	};

	// returns a list of the configured IP interfaces
	// on the machine
	TORRENT_EXTRA_EXPORT std::vector<ip_interface> enum_net_interfaces(io_service& ios
		, error_code& ec);

	TORRENT_EXTRA_EXPORT std::vector<ip_route> enum_routes(io_service& ios
		, error_code& ec);

	// return (a1 & mask) == (a2 & mask)
	TORRENT_EXTRA_EXPORT bool match_addr_mask(address const& a1
		, address const& a2, address const& mask);

	// returns true if the specified address is on the same
	// local network as us
	TORRENT_EXTRA_EXPORT bool in_local_network(io_service& ios, address const& addr
		, error_code& ec);
	TORRENT_EXTRA_EXPORT bool in_local_network(std::vector<ip_interface> const& net
		, address const& addr);

	TORRENT_EXTRA_EXPORT address get_default_gateway(io_service& ios, error_code& ec);

#ifdef SO_BINDTODEVICE
	struct bind_to_device_opt
	{
		bind_to_device_opt(char const* device): m_value(device) {}
		template<class Protocol>
		int level(Protocol const&) const { return SOL_SOCKET; }
		template<class Protocol>
		int name(Protocol const&) const { return SO_BINDTODEVICE; }
		template<class Protocol>
		const char* data(Protocol const&) const { return m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return IFNAMSIZ; }
		char const* m_value;
	};
#endif

	// attempt to bind socket to the device with the specified name. For systems
	// that don't support SO_BINDTODEVICE the socket will be bound to one of the
	// IP addresses of the specified device. In this case it is necessary to
	// verify the local endpoint of the socket once the connection is established.
	// the returned address is the ip the socket was bound to (or address_v4::any()
	// in case SO_BINDTODEVICE succeeded and we don't need to verify it).
	template <class Socket>
	address bind_to_device(io_service& ios, Socket& sock
		, boost::asio::ip::tcp const& protocol
		, char const* device_name, int port, error_code& ec)
	{
		tcp::endpoint bind_ep(address_v4::any(), port);

		address ip = address::from_string(device_name, ec);
		if (!ec)
		{
#if TORRENT_USE_IPV6
			// this is to cover the case where "0.0.0.0" is considered any IPv4 or
			// IPv6 address. If we're asking to be bound to an IPv6 address and
			// providing 0.0.0.0 as the device, turn it into "::"
			if (ip == address_v4::any() && protocol == boost::asio::ip::tcp::v6())
				ip = address_v6::any();
#endif
			bind_ep.address(ip);
			// it appears to be an IP. Just bind to that address
			sock.bind(bind_ep, ec);
			return bind_ep.address();
		}

		ec.clear();

#ifdef SO_BINDTODEVICE
		// try to use SO_BINDTODEVICE here, if that exists. If it fails,
		// fall back to the mechanism we have below
		sock.set_option(bind_to_device_opt(device_name), ec);
		if (ec)
#endif
		{
			ec.clear();
			// TODO: 2 this could be done more efficiently by just looking up
			// the interface with the given name, maybe even with if_nametoindex()
			std::vector<ip_interface> ifs = enum_net_interfaces(ios, ec);
			if (ec) return bind_ep.address();

			bool found = false;

			for (int i = 0; i < int(ifs.size()); ++i)
			{
				// we're looking for a specific interface, and its address
				// (which must be of the same family as the address we're
				// connecting to)
				if (strcmp(ifs[i].name, device_name) != 0) continue;
				if (ifs[i].interface_address.is_v4() != (protocol == boost::asio::ip::tcp::v4()))
					continue;

				bind_ep.address(ifs[i].interface_address);
				found = true;
				break;
			}

			if (!found)
			{
				ec = error_code(boost::system::errc::no_such_device, generic_category());
				return bind_ep.address();
			}
		}
		sock.bind(bind_ep, ec);
		return bind_ep.address();
	}

	// returns true if the given device exists
	TORRENT_EXTRA_EXPORT bool has_interface(char const* name, io_service& ios
		, error_code& ec);

	// returns the device name whose local address is ``addr``. If
	// no such device is found, an empty string is returned.
	TORRENT_EXTRA_EXPORT std::string device_for_address(address addr
		, io_service& ios, error_code& ec);

}

#endif

