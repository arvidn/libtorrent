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
#include "libtorrent/flags.hpp"

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

using flags_t = flags::bitfield_flag<std::uint32_t, struct test_proxy_tag>;

constexpr flags_t expect_http_connection = 1_bit;
constexpr flags_t expect_udp_connection = 2_bit;
//constexpr flags_t expect_http_reject = 3_bit;
//constexpr flags_t expect_udp_reject = 4_bit;
constexpr flags_t expect_dht_msg = 5_bit;
constexpr flags_t expect_peer_connection = 6_bit;

constexpr flags_t dont_proxy_peers = 10_bit;
constexpr flags_t dont_proxy_trackers = 11_bit;

session_proxy test_proxy(settings_pack::proxy_type_t proxy_type, flags_t flags)
{
#ifdef TORRENT_DISABLE_DHT
	// if DHT is disabled, we won't get any requests to it
	flags &= ~expect_dht_msg;
#endif
	std::printf("\n=== TEST == proxy: %s \n\n", proxy_name[proxy_type]);
	int const http_port = start_web_server();
	int const udp_port = start_udp_tracker();
	int const dht_port = start_dht();
	int const peer_port = start_peer();

	int const prev_udp_announces = num_udp_announces();

	settings_pack sett = settings();
	sett.set_int(settings_pack::stop_tracker_timeout, 2);
	sett.set_int(settings_pack::tracker_completion_timeout, 2);
	sett.set_int(settings_pack::tracker_receive_timeout, 2);
	sett.set_bool(settings_pack::announce_to_all_trackers, true);
	sett.set_bool(settings_pack::announce_to_all_tiers, true);
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
	sett.set_bool(settings_pack::proxy_peer_connections, !(flags & dont_proxy_peers));
	sett.set_bool(settings_pack::proxy_tracker_connections, !(flags & dont_proxy_trackers));
	sett.set_int(settings_pack::proxy_port, 4444);

	std::unique_ptr<lt::session> s(new lt::session(sett));

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
	std::printf("http tracker: %s\n", http_tracker_url);

	char udp_tracker_url[200];
	std::snprintf(udp_tracker_url, sizeof(udp_tracker_url)
		, "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(udp_tracker_url, 1);
	std::printf("udp tracker: %s\n", udp_tracker_url);

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

	std::vector<std::string> accepted_trackers;

	int const timeout = 30;
	std::size_t const expected_trackers
		= ((flags & expect_http_connection) ? 2 : 0)
		+ ((flags & expect_udp_connection) ? 2 : 0);

	for (int i = 0; i < timeout; ++i)
	{
		print_alerts(*s, "s", false, false
			, [&](lt::alert const* a)
			{
				if (auto const* ta = alert_cast<tracker_reply_alert>(a))
				{
					std::printf("accepted tracker: %s\n", ta->tracker_url());
					accepted_trackers.push_back(ta->tracker_url());
				}
				return false;
			});
		std::this_thread::sleep_for(lt::milliseconds(100));

		if (num_udp_announces() >= prev_udp_announces + 1
			&& num_peer_hits() > 0
			&& accepted_trackers.size() >= expected_trackers)
		{
			break;
		}
	}

	// we should have announced to the tracker by now
	TEST_EQUAL(num_udp_announces(), prev_udp_announces
		+ ((flags & expect_udp_connection) ? 1 : 0));

	if (flags & expect_dht_msg)
	{
		TEST_CHECK(num_dht_hits() > 0);
	}
	else
	{
		TEST_EQUAL(num_dht_hits(), 0);
	}

	if (flags & expect_peer_connection)
	{
		TEST_CHECK(num_peer_hits() > 0);
	}
	else
	{
		TEST_EQUAL(num_peer_hits(), 0);
	}

	if (flags & expect_http_connection)
	{
		std::printf("expecting: %s\n", http_tracker_url);
		TEST_CHECK(std::find(accepted_trackers.begin(), accepted_trackers.end()
			, http_tracker_url) != accepted_trackers.end());
	}
	else
	{
		std::printf("NOT expecting: %s\n", http_tracker_url);
		TEST_CHECK(std::find(accepted_trackers.begin(), accepted_trackers.end()
			, http_tracker_url) == accepted_trackers.end());
	}

	if (flags & expect_udp_connection)
	{
		std::printf("expecting: %s\n", udp_tracker_url);
		TEST_CHECK(std::find(accepted_trackers.begin(), accepted_trackers.end()
			, udp_tracker_url) != accepted_trackers.end());
	}
	else
	{
		std::printf("NOT expecting: %s\n", udp_tracker_url);
		TEST_CHECK(std::find(accepted_trackers.begin(), accepted_trackers.end()
			, udp_tracker_url) == accepted_trackers.end());
	}

	std::printf("%s: ~session\n", time_now_string());
	session_proxy pr = s->abort();
	s.reset();

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

// since we don't actually have a proxy in this test, make sure libtorrent
// doesn't send any outgoing packets to either tracker or the peer
TORRENT_TEST(socks4)
{
	test_proxy(settings_pack::socks4, {});
}

TORRENT_TEST(socks5)
{
	test_proxy(settings_pack::socks5, {});
}

TORRENT_TEST(socks5_pw)
{
	test_proxy(settings_pack::socks5_pw, {});
}

TORRENT_TEST(http)
{
	test_proxy(settings_pack::http, {});
}

TORRENT_TEST(http_pw)
{
	test_proxy(settings_pack::http_pw, {});
}

// if we configure trackers to not be proxied, they should be let through
TORRENT_TEST(socks4_tracker)
{
	test_proxy(settings_pack::socks4, dont_proxy_trackers | expect_http_connection | expect_udp_connection);
}

TORRENT_TEST(socks5_tracker)
{
	test_proxy(settings_pack::socks5, dont_proxy_trackers | expect_http_connection | expect_udp_connection);
}

TORRENT_TEST(socks5_pw_tracker)
{
	test_proxy(settings_pack::socks5_pw, dont_proxy_trackers | expect_http_connection | expect_udp_connection);
}

TORRENT_TEST(http_tracker)
{
	test_proxy(settings_pack::http, dont_proxy_trackers | expect_http_connection | expect_udp_connection);
}

TORRENT_TEST(http_pw_tracker)
{
	test_proxy(settings_pack::http_pw, dont_proxy_trackers | expect_http_connection | expect_udp_connection);
}

// if we configure peers to not be proxied, they should be let through
TORRENT_TEST(socks4_peer)
{
	test_proxy(settings_pack::socks4, dont_proxy_peers | expect_peer_connection);
}

TORRENT_TEST(socks5_peer)
{
	test_proxy(settings_pack::socks5, dont_proxy_peers | expect_peer_connection);
}

TORRENT_TEST(socks5_pw_peer)
{
	test_proxy(settings_pack::socks5_pw, dont_proxy_peers | expect_peer_connection);
}

TORRENT_TEST(http_peer)
{
	test_proxy(settings_pack::http, dont_proxy_peers | expect_peer_connection);
}

TORRENT_TEST(http_pw_peer)
{
	test_proxy(settings_pack::http_pw, dont_proxy_peers | expect_peer_connection);
}

#if TORRENT_USE_I2P
TORRENT_TEST(i2p)
{
	test_proxy(settings_pack::i2p_proxy, {});
}
#endif
