/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Shared helpers for the port-mapping fuzzers (natpmp, upnp).

#pragma once

#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/portmap.hpp"

// A do-nothing portmap_callback: the fuzzers drive natpmp/upnp directly and
// don't care about the mapping results, and logging is suppressed so fuzzing
// output stays quiet.
struct fuzz_portmap_cb final : lt::aux::portmap_callback
{
	void on_port_mapping(lt::port_mapping_t,
		lt::address const&,
		int,
		lt::portmap_protocol,
		lt::error_code const&,
		lt::portmap_transport,
		lt::aux::listen_socket_handle const&) override
	{}

#ifndef TORRENT_DISABLE_LOGGING
	bool should_log_portmap(lt::portmap_transport) const override { return false; }
	void log_portmap(
		lt::portmap_transport, char const*, lt::aux::listen_socket_handle const&) const override
	{}
#endif
};
