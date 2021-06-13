/*

Copyright (c) 2016-2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

