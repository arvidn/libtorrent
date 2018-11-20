/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/session_settings.hpp"

namespace libtorrent { namespace aux {

namespace {

template <typename Settings>
void init(proxy_settings& p, Settings const& sett)
{
	p.hostname = sett.get_str(settings_pack::proxy_hostname);
	p.username = sett.get_str(settings_pack::proxy_username);
	p.password = sett.get_str(settings_pack::proxy_password);
	p.type = settings_pack::proxy_type_t(sett.get_int(settings_pack::proxy_type));
	p.port = std::uint16_t(sett.get_int(settings_pack::proxy_port));
	p.proxy_hostnames = sett.get_bool(settings_pack::proxy_hostnames);
	p.proxy_peer_connections = sett.get_bool(
		settings_pack::proxy_peer_connections);
	p.proxy_tracker_connections = sett.get_bool(
		settings_pack::proxy_tracker_connections);
}

}

proxy_settings::proxy_settings() = default;

proxy_settings::proxy_settings(settings_pack const& sett)
{ init(*this, sett); }

proxy_settings::proxy_settings(aux::session_settings const& sett)
{ init(*this, sett); }

} // namespace aux
} // namespace libtorrent
