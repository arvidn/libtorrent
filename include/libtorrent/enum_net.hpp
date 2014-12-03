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

#ifndef TORRENT_ENUM_NET_HPP_INCLUDED
#define TORRENT_ENUM_NET_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include <vector>
#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent
{

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
	TORRENT_EXPORT std::vector<ip_interface> enum_net_interfaces(io_service& ios
		, error_code& ec);

	TORRENT_EXPORT std::vector<ip_route> enum_routes(io_service& ios, error_code& ec);

	// return (a1 & mask) == (a2 & mask)
	TORRENT_EXPORT bool match_addr_mask(address const& a1, address const& a2, address const& mask);

	// returns true if the specified address is on the same
	// local network as us
	TORRENT_EXPORT bool in_local_network(io_service& ios, address const& addr
		, error_code& ec);
	
	TORRENT_EXPORT address get_default_gateway(io_service& ios
		, error_code& ec);
}

#endif

