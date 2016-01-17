/*

Copyright (c) 2010-2016, Arvid Norberg
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

#ifndef TORRENT_UNION_ENDPOINT_HPP_INCLUDED
#define TORRENT_UNION_ENDPOINT_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"

namespace libtorrent
{

	struct union_endpoint
	{
		union_endpoint(tcp::endpoint const& ep)
		{
			*this = ep;
		}

		union_endpoint(udp::endpoint const& ep)
		{
			*this = ep;
		}

		union_endpoint()
		{
			*this = tcp::endpoint();
		}

		union_endpoint& operator=(udp::endpoint const& ep)
		{
#if TORRENT_USE_IPV6
			v4 = ep.address().is_v4();
			if (v4)
				addr.v4 = ep.address().to_v4().to_bytes();
			else
				addr.v6 = ep.address().to_v6().to_bytes();
#else
			addr.v4 = ep.address().to_v4().to_bytes();
#endif
			port = ep.port();
			return *this;
		}

		operator udp::endpoint() const
		{
#if TORRENT_USE_IPV6
			if (v4) return udp::endpoint(address_v4(addr.v4), port);
			else return udp::endpoint(address_v6(addr.v6), port);
#else
			return udp::endpoint(address_v4(addr.v4), port);
#endif
		}

		union_endpoint& operator=(tcp::endpoint const& ep)
		{
#if TORRENT_USE_IPV6
			v4 = ep.address().is_v4();
			if (v4)
				addr.v4 = ep.address().to_v4().to_bytes();
			else
				addr.v6 = ep.address().to_v6().to_bytes();
#else
			addr.v4 = ep.address().to_v4().to_bytes();
#endif
			port = ep.port();
			return *this;
		}

		libtorrent::address address() const
		{
#if TORRENT_USE_IPV6
			if (v4) return address_v4(addr.v4);
			else return address_v6(addr.v6);
#else
			return address_v4(addr.v4);
#endif
		}

		operator tcp::endpoint() const
		{
#if TORRENT_USE_IPV6
			if (v4) return tcp::endpoint(address_v4(addr.v4), port);
			else return tcp::endpoint(address_v6(addr.v6), port);
#else
			return tcp::endpoint(address_v4(addr.v4), port);
#endif
		}

		TORRENT_UNION addr_t
		{
			address_v4::bytes_type v4;
#if TORRENT_USE_IPV6
			address_v6::bytes_type v6;
#endif
		} addr;
		boost::uint16_t port;
#if TORRENT_USE_IPV6
		bool v4:1;
#endif
	};
}

#endif

