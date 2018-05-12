/*

Copyright (c) 2016, Arvid Norberg
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

#ifndef TORRENT_UTILS_HPP_INCLUDED
#define TORRENT_UTILS_HPP_INCLUDED

#include <functional>
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "simulator/simulator.hpp"

namespace libtorrent
{
	class session;
	class alert;
}

void utp_only(lt::session& ses);
void enable_enc(lt::session& ses);
void filter_ips(lt::session& ses);
void set_cache_size(lt::session& ses, int val);
int get_cache_size(lt::session& ses);

std::unique_ptr<sim::asio::io_service> make_io_service(
	sim::simulation& sim, int i);

enum flags_t
{
	ipv6 = 1,
};

void set_proxy(lt::session& ses, int proxy_type, int flags = 0
	, bool proxy_peer_connections = true);

void print_alerts(lt::session& ses
	, std::function<void(lt::session&, lt::alert const*)> on_alert
		= [](lt::session&, lt::alert const*) {}, int idx = 0);

#endif

