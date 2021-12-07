/*

Copyright (c) 2015, 2017-2018, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_MAKE_PROXY_SETTINGS_HPP
#define TORRENT_MAKE_PROXY_SETTINGS_HPP

#include "libtorrent/aux_/proxy_settings.hpp"

inline lt::aux::proxy_settings make_proxy_settings(
	lt::settings_pack::proxy_type_t const proxy_type)
{
	using namespace lt;

	aux::proxy_settings ps;
	ps.type = proxy_type;
	ps.proxy_hostnames = false;
	// this IP and ports are specific to test_http_connection.cpp
	if (proxy_type != settings_pack::none)
	{
		ps.hostname = "50.50.50.50";
		ps.port = proxy_type == settings_pack::http ? 4445 : 4444;
		ps.username = "testuser";
		ps.password = "testpass";
	}
	return ps;
}

#endif
