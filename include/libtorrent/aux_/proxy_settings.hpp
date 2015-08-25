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

#include "libtorrent/version.hpp"
#include "libtorrent/config.hpp"

#include <string>
#include <boost/cstdint.hpp>

namespace libtorrent {
struct settings_pack;
namespace aux {

	struct session_settings;

	// The ``proxy_settings`` structs contains the information needed to
	// direct certain traffic to a proxy.
	struct TORRENT_DEPRECATED_EXPORT proxy_settings
	{
		// defaults constructs proxy settings, initializing it to the default
		// settings.
		proxy_settings();

		// construct the proxy_settings object from the settings
		// this constructor is implemented in session_impl.cpp
		proxy_settings(settings_pack const& sett);
		proxy_settings(aux::session_settings const& sett);

		// the name or IP of the proxy server. ``port`` is the port number the
		// proxy listens to. If required, ``username`` and ``password`` can be
		// set to authenticate with the proxy.
		std::string hostname;

		// when using a proy type that requires authentication, the username
		// and password fields must be set to the credentials for the proxy.
		std::string username;
		std::string password;

#ifndef TORRENT_NO_DEPRECATE
		// the type of proxy to use. Assign one of these to the
		// proxy_settings::type field.
		enum proxy_type
		{
			// This is the default, no proxy server is used, all other fields are
			// ignored.
			none,

			// The server is assumed to be a `SOCKS4 server`_ that requires a
			// username.
			//
			// .. _`SOCKS4 server`: http://www.ufasoft.com/doc/socks4_protocol.htm
			socks4,

			// The server is assumed to be a SOCKS5 server (`RFC 1928`_) that does
			// not require any authentication. The username and password are
			// ignored.
			//
			// .. _`RFC 1928`: http://www.faqs.org/rfcs/rfc1928.html
			socks5,

			// The server is assumed to be a SOCKS5 server that supports plain
			// text username and password authentication (`RFC 1929`_). The
			// username and password specified may be sent to the proxy if it
			// requires.
			//
			// .. _`RFC 1929`: http://www.faqs.org/rfcs/rfc1929.html
			socks5_pw,

			// The server is assumed to be an HTTP proxy. If the transport used
			// for the connection is non-HTTP, the server is assumed to support
			// the CONNECT_ method. i.e. for web seeds and HTTP trackers, a plain
			// proxy will suffice. The proxy is assumed to not require
			// authorization. The username and password will not be used.
			//
			// .. _CONNECT: http://tools.ietf.org/html/draft-luotonen-web-proxy-tunneling-01
			http,

			// The server is assumed to be an HTTP proxy that requires user
			// authorization. The username and password will be sent to the proxy.
			http_pw,

			// route through an i2p SAM proxy
			i2p_proxy
		};
#endif

		// tells libtorrent what kind of proxy server it is. See proxy_type
		// enum for options
		boost::uint8_t type;

		// the port the proxy server is running on
		boost::uint16_t port;

		// defaults to true. It means that hostnames should be attempted to be
		// resolved through the proxy instead of using the local DNS service.
		// This is only supported by SOCKS5 and HTTP.
		bool proxy_hostnames;

		// determines whether or not to excempt peer and web seed connections
		// from using the proxy. This defaults to true, i.e. peer connections are
		// proxied by default.
		bool proxy_peer_connections;

		// if true, tracker connections are subject to the proxy settings
		bool proxy_tracker_connections;
	};


}
}

#endif

