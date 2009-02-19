/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/socket_type.hpp"
#include <boost/shared_ptr.hpp>
#include <stdexcept>

namespace libtorrent
{

	bool instantiate_connection(io_service& ios
		, proxy_settings const& ps, socket_type& s)
	{
		if (ps.type == proxy_settings::none)
		{
			s.instantiate<stream_socket>(ios);
		}
		else if (ps.type == proxy_settings::http
			|| ps.type == proxy_settings::http_pw)
		{
			s.instantiate<http_stream>(ios);
			s.get<http_stream>().set_proxy(ps.hostname, ps.port);
			if (ps.type == proxy_settings::http_pw)
				s.get<http_stream>().set_username(ps.username, ps.password);
		}
		else if (ps.type == proxy_settings::socks5
			|| ps.type == proxy_settings::socks5_pw)
		{
			s.instantiate<socks5_stream>(ios);
			s.get<socks5_stream>().set_proxy(ps.hostname, ps.port);
			if (ps.type == proxy_settings::socks5_pw)
				s.get<socks5_stream>().set_username(ps.username, ps.password);
		}
		else if (ps.type == proxy_settings::socks4)
		{
			s.instantiate<socks4_stream>(ios);
			s.get<socks4_stream>().set_proxy(ps.hostname, ps.port);
			s.get<socks4_stream>().set_username(ps.username);
		}
		else
		{
			return false;
		}
		return true;
	}

}

