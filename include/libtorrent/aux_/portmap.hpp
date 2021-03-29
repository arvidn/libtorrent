/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017, Pavel Pimenov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_AUX_PORTMAP_HPP_INCLUDED
#define LIBTORRENT_AUX_PORTMAP_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/portmap.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"

namespace lt::aux {

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
			, portmap_protocol proto, error_code const& ec, portmap_transport transport
			, listen_socket_handle const& ls) = 0;
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log_portmap(portmap_transport transport) const = 0;
		virtual void log_portmap(portmap_transport transport, char const* msg
			, listen_socket_handle const&) const = 0;
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
}

#endif // LIBTORRENT_AUX_PORTMAP_HPP_INCLUDED
