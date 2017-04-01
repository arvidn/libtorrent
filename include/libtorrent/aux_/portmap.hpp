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

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {
namespace aux
{
	// TODO: move this for a better place and integrate it with
	// portmap error alerts
	enum class portmap_transport : std::uint8_t
	{
		natpmp, upnp
	};

	enum class portmap_protocol : std::uint8_t
	{
		none, tcp, udp
	};

	struct TORRENT_EXTRA_EXPORT portmap_callback
	{
		// int: port-mapping index
		// address: external address as queried from router
		// int: external port
		// int: protocol (UDP, TCP)
		// error_code: error, an empty error means success
		// int: transport is 0 for NAT-PMP and 1 for UPnP
		virtual void on_port_mapping(int mapping, address const& ip, int port
			, portmap_protocol proto, error_code const& ec, portmap_transport transport) = 0;
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_portmap(portmap_transport transport) const = 0;
		virtual void log_portmap(portmap_transport transport, char const* msg) const = 0;
#endif

	protected:
		~portmap_callback() {}
	};
}}}

#endif // LIBTORRENT_PORTMAP_HPP
