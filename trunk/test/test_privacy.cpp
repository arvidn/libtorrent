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
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"

#include <fstream>

using namespace libtorrent;

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

bool alert_predicate(libtorrent::alert* a)
{
	anonymous_mode_alert* am = alert_cast<anonymous_mode_alert>(a);
	if (am == NULL) return false;

	if (am->kind == anonymous_mode_alert::tracker_not_anonymous)
		rejected_trackers.push_back(am->str);

	return false;
}

enum flags_t
{
	anonymous_mode = 1,
	expect_http_connection = 2,
	expect_udp_connection = 4,
	expect_http_reject = 8,
	expect_udp_reject = 16,
	expect_dht_msg = 32,
	expect_peer_connection = 64,
};

session_proxy test_proxy(proxy_settings::proxy_type proxy_type, int flags)
{
#ifdef TORRENT_DISABLE_DHT
	// if DHT is disabled, we won't get any requests to it
	flags &= ~expect_dht_msg;
#endif
	fprintf(stderr, "\n=== TEST == proxy: %s anonymous-mode: %s\n\n", proxy_name[proxy_type], (flags & anonymous_mode) ? "yes" : "no");
	int http_port = start_web_server();
	int udp_port = start_udp_tracker();
	int dht_port = start_dht();
	int peer_port = start_peer();

	int prev_udp_announces = num_udp_announces();

	int const alert_mask = alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification;

	session* s = new libtorrent::session(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48875, 49800), "0.0.0.0", 0, alert_mask);

	session_settings sett;
	sett.stop_tracker_timeout = 2;
	sett.tracker_completion_timeout = 2;
	sett.tracker_receive_timeout = 2;
	sett.half_open_limit = 1;
	sett.announce_to_all_trackers = true;
	sett.announce_to_all_tiers = true;
	sett.anonymous_mode = flags & anonymous_mode;
	sett.force_proxy = flags & anonymous_mode;

	// if we don't do this, the peer connection test
	// will be delayed by several seconds, by first
	// trying uTP
	sett.enable_outgoing_utp = false;
	s->set_settings(sett);

	// in non-anonymous mode we circumvent/ignore the proxy if it fails
	// wheras in anonymous mode, we just fail
	proxy_settings ps;
	ps.hostname = "non-existing.com";
	ps.port = 4444;
	ps.type = proxy_type;
	s->set_proxy(ps);

	s->start_dht();

	error_code ec;
	remove_all("tmp1_privacy", ec);
	create_directory("tmp1_privacy", ec);
	std::ofstream file(combine_path("tmp1_privacy", "temporary").c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	char http_tracker_url[200];
	snprintf(http_tracker_url, sizeof(http_tracker_url), "http://127.0.0.1:%d/announce", http_port);
	t->add_tracker(http_tracker_url, 0);

	char udp_tracker_url[200];
	snprintf(udp_tracker_url, sizeof(udp_tracker_url), "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(udp_tracker_url, 1);

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	// we don't want to waste time checking the torrent, just go straight into
	// seeding it, announcing to trackers and connecting to peers
	addp.flags |= add_torrent_params::flag_seed_mode;

	addp.ti = t;
	addp.save_path = "tmp1_privacy";
	addp.dht_nodes.push_back(std::pair<std::string, int>("127.0.0.1", dht_port));
	torrent_handle h = s->add_torrent(addp);

	printf("connect_peer: 127.0.0.1:%d\n", peer_port);
	h.connect_peer(tcp::endpoint(address_v4::from_string("127.0.0.1"), peer_port));

	rejected_trackers.clear();

#ifdef TORRENT_USE_VALGRIND
	const int timeout = 100;
#else
	const int timeout = 20;
#endif

	for (int i = 0; i < timeout; ++i)
	{
		print_alerts(*s, "s", false, false, false, &alert_predicate);
		test_sleep(100);

		if (num_udp_announces() >= prev_udp_announces + 1
			&& num_peer_hits() > 0)
			break;
	}

	// we should have announced to the tracker by now
	TEST_EQUAL(num_udp_announces(), prev_udp_announces + bool(flags & expect_udp_connection));
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

	if (flags & expect_udp_reject)
		TEST_CHECK(std::find(rejected_trackers.begin(), rejected_trackers.end(), udp_tracker_url) != rejected_trackers.end());

	if (flags & expect_http_reject)
		TEST_CHECK(std::find(rejected_trackers.begin(), rejected_trackers.end(), http_tracker_url) != rejected_trackers.end());

	fprintf(stderr, "%s: ~session\n", time_now_string());
	session_proxy pr = s->abort();
	delete s;

	stop_peer();
	stop_dht();
	stop_udp_tracker();
	stop_web_server();
	return pr;
}

int test_main()
{
	session_proxy pr[20];
	// not using anonymous mode
	// UDP fails open if we can't connect to the proxy
	// or if the proxy doesn't support UDP
	pr[0] = test_proxy(proxy_settings::none, expect_udp_connection | expect_http_connection | expect_dht_msg | expect_peer_connection);
	pr[1] = test_proxy(proxy_settings::socks4, expect_udp_connection | expect_dht_msg);
	pr[2] = test_proxy(proxy_settings::socks5, expect_udp_connection | expect_dht_msg);
	pr[3] = test_proxy(proxy_settings::socks5_pw, expect_udp_connection | expect_dht_msg);
	pr[4] = test_proxy(proxy_settings::http, expect_udp_connection | expect_dht_msg);
	pr[5] = test_proxy(proxy_settings::http_pw, expect_udp_connection | expect_dht_msg);
	pr[6] = test_proxy(proxy_settings::i2p_proxy, expect_udp_connection | expect_dht_msg);

	// using anonymous mode

	// anonymous mode doesn't require a proxy when one isn't configured. It could be
	// used with a VPN for instance. This will all changed in 1.0, where anonymous
	// mode is separated from force_proxy
	pr[7] = test_proxy(proxy_settings::none, anonymous_mode | expect_peer_connection);
	pr[8] = test_proxy(proxy_settings::socks4, anonymous_mode | expect_udp_reject);
	pr[9] = test_proxy(proxy_settings::socks5, anonymous_mode);
	pr[10] = test_proxy(proxy_settings::socks5_pw, anonymous_mode);
	pr[11] = test_proxy(proxy_settings::http, anonymous_mode | expect_udp_reject);
	pr[12] = test_proxy(proxy_settings::http_pw, anonymous_mode | expect_udp_reject);
	pr[13] = test_proxy(proxy_settings::i2p_proxy, anonymous_mode);
	return 0;
}

