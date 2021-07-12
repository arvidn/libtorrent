/*

Copyright (c) 2015, Arvid Norberg
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

#include <array>
#include <iostream>
#include "test.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp" // for addr()
#include "disk_io.hpp"

using namespace sim;


std::string make_ep_string(char const* address, bool const is_v6
	, char const* port)
{
	std::string ret;
	if (is_v6) ret += '[';
	ret += address;
	if (is_v6) ret += ']';
	ret += ':';
	ret += port;
	return ret;
}

template <typename Setup, typename HandleAlerts, typename Test>
void run_test(
	Setup const& setup
	, HandleAlerts const& on_alert
	, Test const& test
	, test_transfer_flags_t flags = {}
	, test_disk const disk_constructor = test_disk()
	, lt::seconds const timeout = lt::seconds(60)
	)
{
	using namespace lt;

	const bool use_ipv6 = bool(flags & tx::ipv6);

	char const* peer0_ip[2] = { "50.0.0.1", "feed:face:baad:f00d::1" };
	char const* peer1_ip[2] = { "50.0.0.2", "feed:face:baad:f00d::2" };

	using asio::ip::address;
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

	params.disk_io_constructor = disk_constructor;
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer1_ip[use_ipv6], use_ipv6, "6881"));

	params.disk_io_constructor = test_disk().set_seed();
	ses[1] = std::make_shared<lt::session>(params, ios1);

	setup(*ses[0], *ses[1]);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
		on_alert(ses, a);
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	lt::add_torrent_params atp = ::create_test_torrent(10
		, (flags & tx::v2_only) ? create_torrent::v2_only
		: (flags & tx::v1_only) ? create_torrent::v1_only
		: create_flags_t{});
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

		if (ti->v2())
			TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

		auto downloaded = serialize(*ti);
		auto added = serialize(*torrent);
		TEST_CHECK(downloaded == added);

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

TORRENT_TEST(socks4_tcp)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks4);
			filter_ips(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(socks5_tcp_connect)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks5);
			filter_ips(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(encryption_tcp)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ enable_enc(ses0); enable_enc(ses1); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(no_proxy_tcp_ipv6)
{
	using namespace lt;
	run_test(
		[](lt::session&, lt::session&) {},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		},
		tx::ipv6
	);
}

TORRENT_TEST(no_proxy_utp_ipv6)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ utp_only(ses0); utp_only(ses1); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		},
		tx::ipv6
	);
}

// TODO: the socks server does not support IPv6 addresses yet
/*
TORRENT_TEST(socks5_tcp_ipv6)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks5);
			filter_ips(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		},
		tx::ipv6
	);
}
*/

TORRENT_TEST(no_proxy_tcp)
{
	using namespace lt;
	run_test(
		[](lt::session&, lt::session&) {},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(no_proxy_utp)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ utp_only(ses0); utp_only(ses1); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(encryption_utp)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			enable_enc(ses0);
			enable_enc(ses1);
			utp_only(ses0);
			utp_only(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(socks5_utp)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks5);
			utp_only(ses0);
			filter_ips(ses1);
			utp_only(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

// the purpose of these tests is to make sure that the sessions can't actually
// talk directly to each other. i.e. they are negative tests. If they can talk
// directly to each other, all other tests in here may be broken.
TORRENT_TEST(no_proxy_tcp_banned)
{
	using namespace lt;
	run_test(
		[](lt::session&, lt::session& ses1) { filter_ips(ses1); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), false);
		}
	);
}

TORRENT_TEST(no_proxy_utp_banned)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ utp_only(ses0); utp_only(ses1); filter_ips(ses1); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), false);
		}
	);
}

TORRENT_TEST(v2_only)
{
	using namespace lt;
	std::set<piece_index_t> passed;
	run_test(
		[](lt::session& ses0, lt::session& ses1) {},
		[&](lt::session&, lt::alert const* a) {
			if (auto const* pf = alert_cast<piece_finished_alert>(a))
				passed.insert(pf->piece_index);
		},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
		, tx::v2_only
	);
	TEST_EQUAL(passed.size(), 10);
}

TORRENT_TEST(v2_only_magnet)
{
	using namespace lt;
	std::set<piece_index_t> passed;
	run_test(
		[](lt::session& ses0, lt::session& ses1) {},
		[&](lt::session&, lt::alert const* a) {
			if (auto const* pf = alert_cast<piece_finished_alert>(a))
				passed.insert(pf->piece_index);
		},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
		, tx::v2_only | tx::magnet_download
	);
	TEST_EQUAL(passed.size(), 10);
}

TORRENT_TEST(v1_only)
{
	using namespace lt;
	std::set<piece_index_t> passed;
	run_test(
		[](lt::session& ses0, lt::session& ses1) {},
		[&](lt::session&, lt::alert const* a) {
			if (auto const* pf = alert_cast<piece_finished_alert>(a))
				passed.insert(pf->piece_index);
		},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
		, tx::v1_only
	);
	TEST_EQUAL(passed.size(), 10);
}

TORRENT_TEST(piece_extent_affinity)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			settings_pack p;
			p.set_bool(settings_pack::piece_extent_affinity, true);
			ses0.apply_settings(p);
			ses1.apply_settings(p);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
	);
}

TORRENT_TEST(is_finished)
{
	using namespace lt;
	run_test(
		[](lt::session&, lt::session&) {},
		[](lt::session& ses, lt::alert const* a) {
			if (alert_cast<piece_finished_alert>(a))
			{
				TEST_EQUAL(is_finished(ses), false);
				std::vector<download_priority_t> prio(4, dont_download);
				ses.get_torrents()[0].prioritize_files(prio);
				// applying the priorities is asynchronous. the torrent may not
				// finish immediately
			}
		},
		[](std::shared_ptr<lt::session> ses[2]) {
				TEST_EQUAL(is_finished(*ses[0]), true);
				TEST_EQUAL(is_finished(*ses[1]), true);
		}
	);
}

TORRENT_TEST(v1_only_magnet)
{
	using namespace lt;
	std::set<piece_index_t> passed;
	run_test(
		[](lt::session&, lt::session&) {},
		[&](lt::session&, lt::alert const* a) {
			if (auto const* pf = alert_cast<piece_finished_alert>(a))
				passed.insert(pf->piece_index);
		},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
		, tx::v1_only | tx::magnet_download
	);
	TEST_EQUAL(passed.size(), 10);
}

TORRENT_TEST(disk_full)
{
	using namespace lt;
	run_test(
		[](lt::session&, lt::session&) {},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			// the disk filled up, we failed to complete the download
			TEST_EQUAL(!is_seed(*ses[0]), true);
		}
		, {}
		, test_disk().set_space_left(10 * lt::default_block_size)
	);
}

TORRENT_TEST(disk_full_recover)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session&)
		{
			settings_pack p;
			p.set_int(settings_pack::optimistic_disk_retry, 30);
			ses0.apply_settings(p);
		},
		[](lt::session&, lt::alert const* a) {
			if (auto ta = alert_cast<lt::add_torrent_alert>(a))
			{
				// the torrent has to be auto-managed in order to automatically
				// leave upload mode after it hits disk-full
				ta->handle.set_flags(torrent_flags::auto_managed);
			}
		},
		[](std::shared_ptr<lt::session> ses[2]) {
			// the disk filled up, we failed to complete the download
			TEST_EQUAL(is_seed(*ses[0]), true);
		}
		, {}
		, test_disk().set_space_left(10 * lt::default_block_size).set_recover_full_disk()
		, lt::seconds(65)
	);
}

// Below is a series of tests to transfer torrents with varying pad-file related
// traits
void run_torrent_test(std::shared_ptr<lt::torrent_info> ti)
{
	using namespace lt;

	using asio::ip::address;
	address peer0 = addr("50.0.0.1");
	address peer1 = addr("50.0.0.2");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

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

	pack.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");

	// create session
	std::shared_ptr<lt::session> ses[2];

	// session 0 is a downloader, session 1 is a seed

	params.disk_io_constructor = test_disk();
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(settings_pack::listen_interfaces, "50.0.0.2:6881");

	params.disk_io_constructor = test_disk().set_seed();
	ses[1] = std::make_shared<lt::session>(params, ios1);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	lt::add_torrent_params atp;
	atp.ti = ti;
	atp.save_path = ".";

	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	ses[1]->async_add_torrent(atp);
	auto torrent = atp.ti;

	ses[0]->async_add_torrent(atp);

	sim::timer t(sim, lt::seconds(10), [&](boost::system::error_code const&)
	{
		auto h = ses[0]->get_torrents();
		auto ti = h[0].torrent_file_with_hashes();

		if (ti->v2())
			TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

		auto downloaded = serialize(*ti);
		auto added = serialize(*torrent);
		TEST_CHECK(downloaded == added);

		TEST_CHECK(is_seed(*ses[0]));
		TEST_CHECK(is_seed(*ses[1]));

		h[0].force_recheck();
	});

	sim::timer t2(sim, lt::minutes(1), [&](boost::system::error_code const&)
	{
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

namespace {

std::shared_ptr<lt::torrent_info> test_torrent(lt::file_storage fs, lt::create_flags_t const flags)
{
	lt::create_torrent ct(fs, fs.piece_length(), flags);
	lt::settings_pack pack;
	lt::error_code ec;
	lt::set_piece_hashes(ct, "", pack, test_disk().set_seed()
		, [](lt::piece_index_t p) { std::cout << "."; std::cout.flush();}, ec);

	auto e = ct.generate();
	return std::make_shared<lt::torrent_info>(e);
}

}

TORRENT_TEST(simple_torrent)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3ff0, false}, {0x10, true}}, 0x4000), {}));
}

TORRENT_TEST(odd_last_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x4100, false}, {0x10, true}}, 0x4000), {}));
}

TORRENT_TEST(small_piece_size)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3ff0, false}, {0x10, true}}, 0x2000), {}));
}

TORRENT_TEST(odd_piece_size)
{
	run_torrent_test(test_torrent(make_files(
		{{0x1ffe, false}, {0x1, true}}, 0x1fff), {}));
}

TORRENT_TEST(large_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x5000, false}, {0x100000000 - 0x5000, true}}, 0x100000), {}));
}

TORRENT_TEST(unaligned_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3fff, false}, {0x10, true}}, 0x4000), {}));
}

TORRENT_TEST(piece_size_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x8000, false}, {0x8000, true}}, 0x8000), {}));
}

TORRENT_TEST(block_size_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x4000, false}, {0x4000, true}}, 0x4000), {}));
}

TORRENT_TEST(back_to_back_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3000, false}, {0x800, true}, {0x800, true}}, 0x4000), {}));
}

TORRENT_TEST(small_file_large_piece)
{
	run_torrent_test(test_torrent(make_files(
		{{0x833ed, false}, {0x7cc13, true}, {0x3d, false}, {0x7ffc3, true}, {0x14000, false}}, 0x80000), {}));
}
