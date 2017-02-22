/*

Copyright (c) 2017, Arvid Norberg, Alden Torres
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

#include "libtorrent/alert_manager.hpp"
#include "libtorrent/alert_types.hpp"
#include "test.hpp"
#include "setup_transfer.hpp"

#include <algorithm>

using namespace libtorrent;

TORRENT_TEST(alerts_types)
{
#define TEST_ALERT_TYPE(name, seq, prio) \
	TEST_EQUAL(name::priority, prio); \
	TEST_EQUAL(name::alert_type, seq);

	TEST_ALERT_TYPE(dht_get_peers_reply_alert, 87, 0);
	TEST_ALERT_TYPE(session_error_alert, 90, 0);
	TEST_ALERT_TYPE(dht_live_nodes_alert, 91, 0);

#undef TEST_ALERT_TYPE

	TEST_EQUAL(num_alert_types, 92);
}

TORRENT_TEST(dht_get_peers_reply_alert)
{
	alert_manager mgr(1, dht_get_peers_reply_alert::static_category);

	TEST_EQUAL(mgr.should_post<dht_get_peers_reply_alert>(), true);

	sha1_hash const ih = rand_hash();
	tcp::endpoint const ep1 = rand_tcp_ep(rand_v4);
	tcp::endpoint const ep2 = rand_tcp_ep(rand_v4);
	tcp::endpoint const ep3 = rand_tcp_ep(rand_v4);
#if TORRENT_USE_IPV6
	tcp::endpoint const ep4 = rand_tcp_ep(rand_v6);
	tcp::endpoint const ep5 = rand_tcp_ep(rand_v6);
#else
	tcp::endpoint const ep4 = rand_tcp_ep(rand_v4);
	tcp::endpoint const ep5 = rand_tcp_ep(rand_v4);
#endif
	std::vector<tcp::endpoint> v = {ep1, ep2, ep3, ep4, ep5};

	mgr.emplace_alert<dht_get_peers_reply_alert>(ih, v);

	auto const* a = alert_cast<dht_get_peers_reply_alert>(mgr.wait_for_alert(seconds(0)));
	TEST_CHECK(a != nullptr);

	TEST_EQUAL(a->info_hash, ih);
	TEST_EQUAL(a->num_peers(), 5);

	std::vector<tcp::endpoint> peers = a->peers();
	std::sort(v.begin(), v.end());
	std::sort(peers.begin(), peers.end());
	TEST_CHECK(v == peers);
}

TORRENT_TEST(dht_live_nodes_alert)
{
	alert_manager mgr(1, dht_live_nodes_alert::static_category);

	TEST_EQUAL(mgr.should_post<dht_live_nodes_alert>(), true);

	sha1_hash const ih = rand_hash();
	sha1_hash const h1 = rand_hash();
	sha1_hash const h2 = rand_hash();
	sha1_hash const h3 = rand_hash();
	sha1_hash const h4 = rand_hash();
	sha1_hash const h5 = rand_hash();
	udp::endpoint const ep1 = rand_udp_ep(rand_v4);
	udp::endpoint const ep2 = rand_udp_ep(rand_v4);
	udp::endpoint const ep3 = rand_udp_ep(rand_v4);
#if TORRENT_USE_IPV6
	udp::endpoint const ep4 = rand_udp_ep(rand_v6);
	udp::endpoint const ep5 = rand_udp_ep(rand_v6);
#else
	udp::endpoint const ep4 = rand_udp_ep(rand_v4);
	udp::endpoint const ep5 = rand_udp_ep(rand_v4);
#endif
	std::vector<std::pair<sha1_hash, udp::endpoint>> v;
	v.emplace_back(h1, ep1);
	v.emplace_back(h2, ep2);
	v.emplace_back(h3, ep3);
	v.emplace_back(h4, ep4);
	v.emplace_back(h5, ep5);

	mgr.emplace_alert<dht_live_nodes_alert>(ih, v);

	auto const* a = alert_cast<dht_live_nodes_alert>(mgr.wait_for_alert(seconds(0)));
	TEST_CHECK(a != nullptr);

	TEST_EQUAL(a->node_id, ih);
	TEST_EQUAL(a->num_nodes(), 5);

	auto nodes = a->nodes();
	std::sort(v.begin(), v.end());
	std::sort(nodes.begin(), nodes.end());
	TEST_CHECK(v == nodes);
}
