/*

Copyright (c) 2017-2021, Arvid Norberg
Copyright (c) 2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PORTMAP_HPP_INCLUDED
#define TORRENT_PORTMAP_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/units.hpp"

namespace libtorrent {

	// specify which port-mapping mechanism we use to talk to the gateway
	// to forward ports (e.g. NAT-PMP/PCP vs. UPnP)
	enum class portmap_transport : std::uint8_t
	{
		// NAT-PMP or PCP
		natpmp,
		// UPnP IGD
		upnp
	};

	// the IP transport protocol that a port mapping is for. ``none`` is
	// used to represent the absence of a mapping (e.g. when a mapping has
	// been removed).
	enum class portmap_protocol : std::uint8_t
	{
		none, tcp, udp
	};

	// this type represents an index referring to a port mapping
	using port_mapping_t = aux::strong_typedef<int, struct port_mapping_tag>;

}

#endif  //TORRENT_PORTMAP_HPP_INCLUDED
