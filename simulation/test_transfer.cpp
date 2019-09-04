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
#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "setup_transfer.hpp" // for addr()

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
	, int flags = 0)
{
	using namespace lt;

	const bool use_ipv6 = flags & ipv6;

	char const* peer0_ip[2] = { "50.0.0.1", "feed:face:baad:f00d::1" };
	char const* peer1_ip[2] = { "50.0.0.2", "feed:face:baad:f00d::2" };

	using asio::ip::address;
	address peer0 = addr(peer0_ip[use_ipv6]);
	address peer1 = addr(peer1_ip[use_ipv6]);
	address proxy = (flags & ipv6) ? addr("2001::2") : addr("50.50.50.50");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_service ios0 { sim, peer0 };
	sim::asio::io_service ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	sim::asio::io_service proxy_ios{sim, proxy };
	sim::socks_server socks4(proxy_ios, 4444, 4);
	sim::socks_server socks5(proxy_ios, 5555, 5);

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();

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
	ses[0] = std::make_shared<lt::session>(pack, ios0);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer1_ip[use_ipv6], use_ipv6, "6881"));
	ses[1] = std::make_shared<lt::session>(pack, ios1);

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

	// the first peer is a downloader, the second peer is a seed
	lt::add_torrent_params params = ::create_torrent(1);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;

	params.save_path = save_path(0);
	ses[0]->async_add_torrent(params);

	params.save_path = save_path(1);
	ses[1]->async_add_torrent(params);

	sim::timer t(sim, lt::seconds(60), [&](boost::system::error_code const&)
	{
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
		ipv6
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
		ipv6
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
		ipv6
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

TORRENT_TEST(auto_disk_cache_size)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session&) { set_cache_size(ses0, -1); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);

			int const cache_size = get_cache_size(*ses[0]);
			std::printf("cache size: %d\n", cache_size);
			// this assumes the test torrent is at least 4 blocks
			TEST_CHECK(cache_size > 4);
		}
	);
}

TORRENT_TEST(disable_disk_cache)
{
	using namespace lt;
	run_test(
		[](lt::session& ses0, lt::session&) { set_cache_size(ses0, 0); },
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);

			int const cache_size = get_cache_size(*ses[0]);
			std::printf("cache size: %d\n", cache_size);
			TEST_EQUAL(cache_size, 0);
		}
	);
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
				TEST_EQUAL(is_finished(ses), true);
			}
		},
		[](std::shared_ptr<lt::session> ses[2]) {
				TEST_EQUAL(is_finished(*ses[0]), true);
				TEST_EQUAL(is_finished(*ses[1]), true);
		}
	);
}

