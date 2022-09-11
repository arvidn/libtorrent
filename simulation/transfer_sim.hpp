/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TRANSFER_SIM_HPP
#define TORRENT_TRANSFER_SIM_HPP

#include <string>
#include <array>
#include <iostream>

#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/random.hpp"

#include "test.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp" // for addr()
#include "disk_io.hpp"

template <typename Setup, typename HandleAlerts, typename Test>
void run_test(
	Setup setup
	, HandleAlerts on_alert
	, Test test
	, test_transfer_flags_t flags = {}
	, test_disk const downloader_disk_constructor = test_disk()
	, test_disk const seed_disk_constructor = test_disk()
	, lt::seconds const timeout = lt::seconds(60)
	)
{
	using lt::settings_pack;
	using lt::address;
	using lt::alert_cast;

	const bool use_ipv6 = bool(flags & tx::ipv6);

	char const* peer0_ip[2] = { "50.0.0.1", "feed:face:baad:f00d::1" };
	char const* peer1_ip[2] = { "50.0.0.2", "feed:face:baad:f00d::2" };

	address peer0 = addr(peer0_ip[use_ipv6]);
	address peer1 = addr(peer1_ip[use_ipv6]);
	address proxy = (flags & tx::ipv6) ? addr("2001::2") : addr("50.50.50.50");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	sim::asio::io_context proxy_ios{sim, proxy };
	sim::socks_server socks4(proxy_ios, 4444, 4);
	sim::socks_server socks5(proxy_ios, 5555, 5);
	socks5.bind_start_port(3000);

	lt::session_params params;
	// setup settings pack to use for the session (customization point)
	lt::settings_pack& pack = params.settings;
	pack = settings();
	pack.set_bool(settings_pack::disable_hash_checks, false);

	// disable utp by default
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);

	// disable encryption by default
	pack.set_bool(settings_pack::prefer_rc4, false);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_plaintext);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer0_ip[use_ipv6], use_ipv6, "6881"));

	// create session
	std::shared_ptr<lt::session> ses[2];

	// session 0 is a downloader, session 1 is a seed

	params.disk_io_constructor = downloader_disk_constructor;
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer1_ip[use_ipv6], use_ipv6, "6881"));

	params.disk_io_constructor = seed_disk_constructor.set_files(existing_files_mode::full_valid);
	ses[1] = std::make_shared<lt::session>(params, ios1);

	setup(*ses[0], *ses[1]);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			if (flags & tx::connect_proxy)
				ta->handle.connect_peer(lt::tcp::endpoint(proxy, 3000));
			else
				ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
		on_alert(ses, a);
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	lt::add_torrent_params atp = ::create_test_torrent(10
		, (flags & tx::v2_only) ? lt::create_torrent::v2_only
		: (flags & tx::v1_only) ? lt::create_torrent::v1_only
		: lt::create_flags_t{}
		, (flags & tx::small_pieces) ? 1 : (flags & tx::large_pieces) ? 4 : 2
		, (flags & tx::multiple_files) ? 3 : 1
		);
	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	ses[1]->async_add_torrent(atp);
	auto torrent = atp.ti;

	atp.save_path = save_path(0);
	if (flags & tx::magnet_download)
	{
		atp.info_hashes = atp.ti->info_hashes();
		atp.ti.reset();
	}
	ses[0]->async_add_torrent(atp);

	sim::timer t(sim, timeout, [&](boost::system::error_code const&)
	{
		auto h = ses[0]->get_torrents();
		auto ti = h[0].torrent_file_with_hashes();

		// if we're a seed, we should definitely have the torrent info. If we're
		// note a seed, we may still have the torrent_info in case it's a v1
		// torrent
		if (is_seed(*ses[0])) TEST_CHECK(ti);

		if (ti)
		{
			if (ti->v2())
				TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

#if TORRENT_ABI_VERSION < 4
			{
				auto downloaded = serialize(*ti);
				auto added = serialize(*torrent);
				TEST_CHECK(downloaded == added);
			}
#endif
		}

		test(ses);

		// shut down
		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim.run();
}

void no_init(lt::session& ses0, lt::session& ses1);

struct record_finished_pieces
{
	record_finished_pieces(std::set<lt::piece_index_t>& p);
	void operator()(lt::session&, lt::alert const* a) const;

	std::set<lt::piece_index_t>* m_passed;
};

struct expect_seed
{
	expect_seed(bool e);
	void operator()(std::shared_ptr<lt::session> ses[2]) const;
	bool m_expect;
};

int blocks_per_piece(test_transfer_flags_t const flags);
int num_pieces(test_transfer_flags_t const flags);

#endif
