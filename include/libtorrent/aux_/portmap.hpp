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

#ifndef LIBTORRENT_AUX_PORTMAP_HPP_INCLUDED
#define LIBTORRENT_AUX_PORTMAP_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/portmap.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent {
namespace aux {

	enum class portmap_action : std::uint8_t
	{
		none, add, del
	};

	struct TORRENT_EXTRA_EXPORT portmap_callback
	{
		// int: port-mapping index
		// address: external address as queried from router
		// int: external port
		// int: protocol (UDP, TCP)
		// error_code: error, an empty error means success
		// int: transport is 0 for NAT-PMP and 1 for UPnP
		virtual void on_port_mapping(port_mapping_t mapping, address const& ip, int port
			, portmap_protocol proto, error_code const& ec, portmap_transport transport) = 0;
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_portmap(portmap_transport transport) const = 0;
		virtual void log_portmap(portmap_transport transport, char const* msg) const = 0;
#endif

	protected:
		~portmap_callback() {}
	};

	struct base_mapping
	{
		// the time the port mapping will expire
		time_point expires;

		portmap_action act = portmap_action::none;

		// the external (on the NAT router) port
		// for the mapping. This is the port we
		// should announce to others
		int external_port = 0;

		portmap_protocol protocol = portmap_protocol::none;
	};

	inline char const* to_string(portmap_protocol const p)
	{
		return p == portmap_protocol::udp ? "UDP" : "TCP";
	}
	inline char const* to_string(portmap_action const act)
	{
		switch (act)
		{
			case portmap_action::none: return "none";
			case portmap_action::add: return "add";
			case portmap_action::del: return "delete";
		}
		return "";
	}
}}

#endif // LIBTORRENT_AUX_PORTMAP_HPP_INCLUDED
