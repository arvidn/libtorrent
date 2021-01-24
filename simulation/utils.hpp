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
#include "libtorrent/flags.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/settings_pack.hpp"
#include "simulator/simulator.hpp"

namespace libtorrent
{
	struct session;
	struct alert;
}

// adds an IP filter to disallow 50.0.0.1 and 50.0.0.2
void filter_ips(lt::session& ses);

bool has_metadata(lt::session& ses);
bool is_seed(lt::session& ses);
bool is_finished(lt::session& ses);
int completed_pieces(lt::session& ses);
void add_extra_peers(lt::session& ses);
lt::torrent_status get_status(lt::session& ses);

// disable TCP and enable uTP
void utp_only(lt::session& ses);
void utp_only(lt::settings_pack& pack);

// force encrypted connections
void enable_enc(lt::session& ses);
void enable_enc(lt::settings_pack& pack);

std::string save_path(int swarm_id, int idx);

std::unique_ptr<sim::asio::io_context> make_io_context(
	sim::simulation& sim, int i);

using lt::operator""_bit;

using test_transfer_flags_t = libtorrent::flags::bitfield_flag<std::uint32_t, struct test_transfer_flags_tag>;

namespace tx {
constexpr test_transfer_flags_t ipv6 = 0_bit;
constexpr test_transfer_flags_t v1_only = 1_bit;
constexpr test_transfer_flags_t v2_only = 2_bit;
constexpr test_transfer_flags_t magnet_download = 3_bit;
constexpr test_transfer_flags_t proxy_peers = 4_bit;
}

void set_proxy(lt::session& ses, int proxy_type, test_transfer_flags_t flags = tx::proxy_peers);

void print_alerts(lt::session& ses
	, std::function<void(lt::session&, lt::alert const*)> on_alert
		= [](lt::session&, lt::alert const*) {}, int idx = 0);

#endif

