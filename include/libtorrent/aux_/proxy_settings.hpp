/*

Copyright (c) 2003-2015, Arvid Norberg
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

#ifndef TORRENT_PROXY_SETTINGS_HPP_INCLUDED
#define TORRENT_PROXY_SETTINGS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"

#include <string>

namespace libtorrent {

struct settings_pack;
namespace aux {

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

}}

#endif
