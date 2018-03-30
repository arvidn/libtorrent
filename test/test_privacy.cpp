/*

Copyright (c) 2013, Arvid Norberg
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

#include "test.hpp"
#include "setup_transfer.hpp"
#include "dht_server.hpp"
#include "peer_server.hpp"
#include "udp_tracker.hpp"
#include "test_utils.hpp"
#include "settings.hpp"

#include "libtorrent/alert.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/path.hpp"

#include <fstream>

using namespace lt;

namespace {

char const* proxy_name[] = {
	"none",
	"socks4",
	"socks5",
	"socks5_pw",
	"http",
	"http_pw",
	"i2p_proxy"
};

std::vector<std::string> rejected_trackers;

bool alert_predicate(lt::alert const* a)
{
	anonymous_mode_alert const* am = alert_cast<anonymous_mode_alert>(a);
	if (am == nullptr) return false;

	if (am->kind == anonymous_mode_alert::tracker_not_anonymous)
		rejected_trackers.push_back(am->str);

	return false;
}

enum flags_t
{
	force_proxy_mode = 1,
	expect_http_connection = 2,
	expect_udp_connection = 4,
	expect_http_reject = 8,
	expect_udp_reject = 16,
	expect_dht_msg = 32,
	expect_peer_connection = 64,
	expect_possible_udp_connection = 128,
	expect_possible_dht_msg = 256,
};

session_proxy test_proxy(settings_pack::proxy_type_t proxy_type, int flags)
{
#ifdef TORRENT_DISABLE_DHT
	// if DHT is disabled, we won't get any requests to it
	flags &= ~expect_dht_msg;
#endif
	std::printf("\n=== TEST == proxy: %s anonymous-mode: %s\n\n"
		, proxy_name[proxy_type], (flags & force_proxy_mode) ? "yes" : "no");
	int const http_port = start_web_server();
	int const udp_port = start_udp_tracker();
	int const dht_port = start_dht();
	int const peer_port = start_peer();

	int const prev_udp_announces = num_udp_announces();

	auto const alert_mask = ~(
			alert::performance_warning
#ifndef TORRENT_NO_DEPRECATE
			| alert::progress_notification
#endif
			| alert::stats_notification);

	settings_pack sett = settings();
	sett.set_int(settings_pack::stop_tracker_timeout, 2);
	sett.set_int(settings_pack::tracker_completion_timeout, 2);
	sett.set_int(settings_pack::tracker_receive_timeout, 2);
	sett.set_bool(settings_pack::announce_to_all_trackers, true);
	sett.set_bool(settings_pack::announce_to_all_tiers, true);
	sett.set_bool(settings_pack::force_proxy, flags & force_proxy_mode);
	sett.set_int(settings_pack::alert_mask, alert_mask);
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_dht, true);

	// since multiple sessions may exist simultaneously (because of the
	// pipelining of the tests) they actually need to use different ports
	static int listen_port = 10000 + int(lt::random(50000));
	char iface[200];
	std::snprintf(iface, sizeof(iface), "127.0.0.1:%d", listen_port);
	listen_port += lt::random(10) + 1;
	sett.set_str(settings_pack::listen_interfaces, iface);

	// if we don't do this, the peer connection test
	// will be delayed by several seconds, by first
	// trying uTP
	sett.set_bool(settings_pack::enable_outgoing_utp, false);

	// in non-anonymous mode we circumvent/ignore the proxy if it fails
	// wheras in anonymous mode, we just fail
	sett.set_str(settings_pack::proxy_hostname, "non-existing.com");
	sett.set_int(settings_pack::proxy_type, proxy_type);
	sett.set_int(settings_pack::proxy_port, 4444);

	lt::session* s = new lt::session(sett);

	error_code ec;
	remove_all("tmp1_privacy", ec);
	create_directory("tmp1_privacy", ec);
	std::ofstream file(combine_path("tmp1_privacy", "temporary").c_str());
	std::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 16 * 1024, 13, false);
	file.close();

	char http_tracker_url[200];
	std::snprintf(http_tracker_url, sizeof(http_tracker_url)
		, "http://127.0.0.1:%d/announce", http_port);
	t->add_tracker(http_tracker_url, 0);

	char udp_tracker_url[200];
	std::snprintf(udp_tracker_url, sizeof(udp_tracker_url)
		, "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(udp_tracker_url, 1);

	add_torrent_params addp;
	addp.flags &= ~torrent_flags::paused;
	addp.flags &= ~torrent_flags::auto_managed;

	// we don't want to waste time checking the torrent, just go straight into
	// seeding it, announcing to trackers and connecting to peers
	addp.flags |= torrent_flags::seed_mode;

	addp.ti = t;
	addp.save_path = "tmp1_privacy";
	addp.dht_nodes.push_back(std::pair<std::string, int>("127.0.0.1", dht_port));
	torrent_handle h = s->add_torrent(addp);

	std::printf("connect_peer: 127.0.0.1:%d\n", peer_port);
	h.connect_peer({address_v4::from_string("127.0.0.1"), std::uint16_t(peer_port)});

	rejected_trackers.clear();

	const int timeout = 20;

	for (int i = 0; i < timeout; ++i)
	{
		print_alerts(*s, "s", false, false, &alert_predicate);
		std::this_thread::sleep_for(lt::milliseconds(100));

		if (num_udp_announces() >= prev_udp_announces + 1
			&& num_peer_hits() > 0)
			break;
	}

	// we should have announced to the tracker by now
	if (flags & expect_possible_udp_connection)
	{
		// this flag is true if we may fail open, but also might not have had
		// enough time to fail yet
		TEST_CHECK(num_udp_announces() == prev_udp_announces
			|| num_udp_announces() == prev_udp_announces + 1);
	}
	else
	{
		TEST_EQUAL(num_udp_announces(), prev_udp_announces
			+ ((flags & expect_udp_connection) != 0 ? 1 : 0));
	}

	if (flags & expect_possible_udp_connection)
	{
		// this flag is true if we may fail open, but also might not have had
		// enough time to fail yet
		TEST_CHECK(num_dht_hits() == 0 || num_dht_hits() == 1);
	}
	else
	{
		if (flags & expect_dht_msg)
		{
			TEST_CHECK(num_dht_hits() > 0);
		}
		else
		{
			TEST_EQUAL(num_dht_hits(), 0);
		}
	}

	if (flags & expect_peer_connection)
	{
		TEST_CHECK(num_peer_hits() > 0);
	}
	else
	{
		TEST_EQUAL(num_peer_hits(), 0);
	}

	if (flags & expect_udp_reject)
		TEST_CHECK(std::find(rejected_trackers.begin(), rejected_trackers.end()
			, udp_tracker_url) != rejected_trackers.end());

	if (flags & expect_http_reject)
		TEST_CHECK(std::find(rejected_trackers.begin(), rejected_trackers.end()
			, http_tracker_url) != rejected_trackers.end());

	std::printf("%s: ~session\n", time_now_string());
	session_proxy pr = s->abort();
	delete s;

	stop_peer();
	stop_dht();
	stop_udp_tracker();
	stop_web_server();
	return pr;
}

} // anonymous namespace

// not using anonymous mode
// UDP fails open if we can't connect to the proxy
// or if the proxy doesn't support UDP

TORRENT_TEST(no_proxy)
{
	test_proxy(settings_pack::none, expect_udp_connection
		| expect_http_connection | expect_dht_msg | expect_peer_connection);
}

TORRENT_TEST(socks4)
{
	test_proxy(settings_pack::socks4, expect_udp_connection | expect_dht_msg);
}

TORRENT_TEST(socks5)
{
	test_proxy(settings_pack::socks5, expect_possible_udp_connection
		| expect_possible_dht_msg);
}

TORRENT_TEST(socks5_pw)
{
	test_proxy(settings_pack::socks5_pw,expect_possible_udp_connection
		| expect_possible_dht_msg);
}

TORRENT_TEST(http)
{
	test_proxy(settings_pack::http, expect_udp_connection | expect_dht_msg);
}

TORRENT_TEST(http_pt)
{
	test_proxy(settings_pack::http_pw, expect_udp_connection | expect_dht_msg);
}

#if TORRENT_USE_I2P
TORRENT_TEST(i2p)
{
	test_proxy(settings_pack::i2p_proxy, expect_udp_connection | expect_dht_msg);
}
#endif

// using anonymous mode

// anonymous mode doesn't require a proxy when one isn't configured. It could be
// used with a VPN for instance. This will all changed in 1.0, where anonymous
// mode is separated from force_proxy

TORRENT_TEST(anon_no_proxy)
{
	test_proxy(settings_pack::none, force_proxy_mode | expect_peer_connection);
}

TORRENT_TEST(anon_socks4)
{
	test_proxy(settings_pack::socks4, force_proxy_mode | expect_udp_reject);
}

TORRENT_TEST(anon_socks5)
{
	test_proxy(settings_pack::socks5, force_proxy_mode);
}

TORRENT_TEST(anon_socks5_pw)
{
	test_proxy(settings_pack::socks5_pw, force_proxy_mode);
}

TORRENT_TEST(anon_http)
{
	test_proxy(settings_pack::http, force_proxy_mode | expect_udp_reject);
}

TORRENT_TEST(anon_http_pw)
{
	test_proxy(settings_pack::http_pw, force_proxy_mode | expect_udp_reject);
}

#if TORRENT_USE_I2P
TORRENT_TEST(anon_i2p)
{
	test_proxy(settings_pack::i2p_proxy, force_proxy_mode);
}
#endif
