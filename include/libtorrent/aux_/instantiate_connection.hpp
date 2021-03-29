/*

Copyright (c) 2007, 2010, 2012, 2015, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_INSTANTIATE_CONNECTION
#define TORRENT_INSTANTIATE_CONNECTION

#include "libtorrent/aux_/export.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/socket_type.hpp"

namespace lt {

	struct utp_socket_manager;

namespace aux {

	// instantiate a socket_type (s) according to the specified criteria
	TORRENT_EXTRA_EXPORT
	aux::socket_type instantiate_connection(io_context&
		, aux::proxy_settings const& ps
		, void* ssl_context
		, utp_socket_manager* sm
		, bool peer_connection
		, bool tracker_connection);
}}

#endif
