/*

Copyright (c) 2015-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PROXY_SETTINGS_HPP_INCLUDED
#define TORRENT_PROXY_SETTINGS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/fwd.hpp"

#include <string>

namespace lt::aux {

	struct session_settings;

	struct TORRENT_EXPORT proxy_settings
	{
		// defaults constructs proxy settings, initializing it to the default
		// settings.
		proxy_settings();

		// construct the proxy_settings object from the settings
		// this constructor is implemented in session_impl.cpp
		explicit proxy_settings(settings_pack const& sett);
		explicit proxy_settings(aux::session_settings const& sett);

		// the name or IP of the proxy server. ``port`` is the port number the
		// proxy listens to. If required, ``username`` and ``password`` can be
		// set to authenticate with the proxy.
		std::string hostname;

		// when using a proxy type that requires authentication, the username
		// and password fields must be set to the credentials for the proxy.
		std::string username;
		std::string password;

		// tells libtorrent what kind of proxy server it is. See proxy_type_t
		// enum for options
		settings_pack::proxy_type_t type = settings_pack::none;

		// the port the proxy server is running on
		std::uint16_t port = 0;

		// defaults to true. It means that hostnames should be attempted to be
		// resolved through the proxy instead of using the local DNS service.
		// This is only supported by SOCKS5 and HTTP.
		bool proxy_hostnames = true;

		// determines whether or not to exempt peer and web seed connections
		// from using the proxy. This defaults to true, i.e. peer connections are
		// proxied by default.
		bool proxy_peer_connections = true;

		// if true, tracker connections are subject to the proxy settings
		bool proxy_tracker_connections = true;
	};
}

#endif
