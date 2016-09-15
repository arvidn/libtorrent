/*

Copyright (c) 2016, Arvid Norberg, Alden Torres
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

#ifndef LIBTORRENT_PORTMAP_HPP
#define LIBTORRENT_PORTMAP_HPP

#include "libtorrent/config.hpp"

namespace libtorrent
{
	struct TORRENT_EXTRA_EXPORT portmap_callback
	{
		constexpr static int map_transport_natpmp = 0;
		constexpr static int map_transport_upnp = 1;

		// int: port-mapping index
		// address: external address as queried from router
		// int: external port
		// int: protocol (UDP, TCP)
		// error_code: error, an empty error means success
		// int: transport is 0 for NAT-PMP and 1 for UPnP
		virtual void on_port_mapping(int mapping, address const& ip, int port
			, int protocol, error_code const& ec, int map_transport) = 0;
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_portmap(int map_transport) const = 0;
		virtual void log_portmap(int map_transport, char const* msg) const = 0;
#endif

	protected:
		~portmap_callback() {}
	};
}

#endif // LIBTORRENT_PORTMAP_HPP
