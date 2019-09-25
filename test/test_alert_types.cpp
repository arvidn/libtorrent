/*

Copyright (c) 2017-2019, Arvid Norberg
Copyright (c) 2017, Alden Torres
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

using namespace lt;

TORRENT_TEST(alerts_types)
{
	// this counter is incremented sequentially
	// with each call to TEST_ALERT_TYPE
	// it starts at 3 because the first alerts
	// are abstract
	int count_alert_types = 3;

#if TORRENT_ABI_VERSION == 1
	TEST_EQUAL(torrent_alert::alert_type, 0);
	TEST_EQUAL(peer_alert::alert_type, 1);
	TEST_EQUAL(tracker_alert::alert_type, 2);
	TEST_EQUAL(alert::debug_notification, alert::connect_notification);
#endif

#define TEST_ALERT_TYPE(name, seq, prio, cat) \
	TEST_CHECK(name::priority == prio); \
	TEST_EQUAL(name::alert_type, seq); \
	TEST_EQUAL(name::static_category, cat); \
	TEST_EQUAL(count_alert_types, seq); \
	TEST_EQUAL(std::string(alert_name(name::alert_type)) + "_alert", #name); \
	count_alert_types++

#if TORRENT_ABI_VERSION == 1
	TEST_ALERT_TYPE(torrent_added_alert, 3, alert_priority::normal, alert::status_notification);
#else
	++count_alert_types;
#endif

#if TORRENT_ABI_VERSION == 1
#define PROGRESS_NOTIFICATION alert::progress_notification |
#else
#define PROGRESS_NOTIFICATION
#endif

	TEST_ALERT_TYPE(torrent_removed_alert, 4, alert_priority::critical, alert::status_notification);
	TEST_ALERT_TYPE(read_piece_alert, 5, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(file_completed_alert, 6, alert_priority::normal, PROGRESS_NOTIFICATION alert::file_progress_notification);
	TEST_ALERT_TYPE(file_renamed_alert, 7, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(file_rename_failed_alert, 8, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(performance_alert, 9, alert_priority::normal, alert::performance_warning);
	TEST_ALERT_TYPE(state_changed_alert, 10, alert_priority::high, alert::status_notification);
	TEST_ALERT_TYPE(tracker_error_alert, 11, alert_priority::high, alert::tracker_notification | alert::error_notification);
	TEST_ALERT_TYPE(tracker_warning_alert, 12, alert_priority::normal, alert::tracker_notification | alert::error_notification);
	TEST_ALERT_TYPE(scrape_reply_alert, 13, alert_priority::critical, alert::tracker_notification);
	TEST_ALERT_TYPE(scrape_failed_alert, 14, alert_priority::critical, alert::tracker_notification | alert::error_notification);
	TEST_ALERT_TYPE(tracker_reply_alert, 15, alert_priority::normal, alert::tracker_notification);
	TEST_ALERT_TYPE(dht_reply_alert, 16, alert_priority::normal, alert::dht_notification | alert::tracker_notification);
	TEST_ALERT_TYPE(tracker_announce_alert, 17, alert_priority::normal, alert::tracker_notification);
	TEST_ALERT_TYPE(hash_failed_alert, 18, alert_priority::normal, alert::status_notification);
	TEST_ALERT_TYPE(peer_ban_alert, 19, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(peer_unsnubbed_alert, 20, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(peer_snubbed_alert, 21, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(peer_error_alert, 22, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(peer_connect_alert, 23, alert_priority::normal, alert::connect_notification);
	TEST_ALERT_TYPE(peer_disconnected_alert, 24, alert_priority::normal, alert::connect_notification);
	TEST_ALERT_TYPE(invalid_request_alert, 25, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(torrent_finished_alert, 26, alert_priority::high, alert::status_notification);
	TEST_ALERT_TYPE(piece_finished_alert, 27, alert_priority::normal, PROGRESS_NOTIFICATION alert::piece_progress_notification);
	TEST_ALERT_TYPE(request_dropped_alert, 28, alert_priority::normal, PROGRESS_NOTIFICATION alert::block_progress_notification | alert::peer_notification);
	TEST_ALERT_TYPE(block_timeout_alert, 29, alert_priority::normal, PROGRESS_NOTIFICATION alert::block_progress_notification | alert::peer_notification);
	TEST_ALERT_TYPE(block_finished_alert, 30, alert_priority::normal, PROGRESS_NOTIFICATION alert::block_progress_notification);
	TEST_ALERT_TYPE(block_downloading_alert, 31, alert_priority::normal, PROGRESS_NOTIFICATION alert::block_progress_notification);
	TEST_ALERT_TYPE(unwanted_block_alert, 32, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(storage_moved_alert, 33, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(storage_moved_failed_alert, 34, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(torrent_deleted_alert, 35, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(torrent_delete_failed_alert, 36, alert_priority::critical, alert::storage_notification | alert::error_notification);
	TEST_ALERT_TYPE(save_resume_data_alert, 37, alert_priority::critical, alert::storage_notification);
	TEST_ALERT_TYPE(save_resume_data_failed_alert, 38, alert_priority::critical, alert::storage_notification | alert::error_notification);
	TEST_ALERT_TYPE(torrent_paused_alert, 39, alert_priority::high, alert::status_notification);
	TEST_ALERT_TYPE(torrent_resumed_alert, 40, alert_priority::high, alert::status_notification);
	TEST_ALERT_TYPE(torrent_checked_alert, 41, alert_priority::high, alert::status_notification);
	TEST_ALERT_TYPE(url_seed_alert, 42, alert_priority::normal, alert::peer_notification | alert::error_notification);
	TEST_ALERT_TYPE(file_error_alert, 43, alert_priority::high, alert::status_notification | alert::error_notification | alert::storage_notification);
	TEST_ALERT_TYPE(metadata_failed_alert, 44, alert_priority::normal, alert::error_notification);
	TEST_ALERT_TYPE(metadata_received_alert, 45, alert_priority::normal, alert::status_notification);
	TEST_ALERT_TYPE(udp_error_alert, 46, alert_priority::normal, alert::error_notification);
	TEST_ALERT_TYPE(external_ip_alert, 47, alert_priority::normal, alert::status_notification);
	TEST_ALERT_TYPE(listen_failed_alert, 48, alert_priority::critical, alert::status_notification | alert::error_notification);
	TEST_ALERT_TYPE(listen_succeeded_alert, 49, alert_priority::critical, alert::status_notification);
	TEST_ALERT_TYPE(portmap_error_alert, 50, alert_priority::normal, alert::port_mapping_notification | alert::error_notification);
	TEST_ALERT_TYPE(portmap_alert, 51, alert_priority::normal, alert::port_mapping_notification);
	TEST_ALERT_TYPE(portmap_log_alert, 52, alert_priority::normal, alert::port_mapping_log_notification);
	TEST_ALERT_TYPE(fastresume_rejected_alert, 53, alert_priority::critical, alert::status_notification | alert::error_notification);
	TEST_ALERT_TYPE(peer_blocked_alert, 54, alert_priority::normal, alert::ip_block_notification);
	TEST_ALERT_TYPE(dht_announce_alert, 55, alert_priority::normal, alert::dht_notification);
	TEST_ALERT_TYPE(dht_get_peers_alert, 56, alert_priority::normal, alert::dht_notification);
	TEST_ALERT_TYPE(stats_alert, 57, alert_priority::normal, alert::stats_notification);
	TEST_ALERT_TYPE(cache_flushed_alert, 58, alert_priority::high, alert::storage_notification);
#if TORRENT_ABI_VERSION == 1
	TEST_ALERT_TYPE(anonymous_mode_alert, 59, alert_priority::normal, alert::error_notification);
#else
	count_alert_types++;
#endif
	TEST_ALERT_TYPE(lsd_peer_alert, 60, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(trackerid_alert, 61, alert_priority::normal, alert::status_notification);
	TEST_ALERT_TYPE(dht_bootstrap_alert, 62, alert_priority::normal, alert::dht_notification);
	count_alert_types++; // 63 is gone
	TEST_ALERT_TYPE(torrent_error_alert, 64, alert_priority::high, alert::error_notification | alert::status_notification);
	TEST_ALERT_TYPE(torrent_need_cert_alert, 65, alert_priority::critical, alert::status_notification);
	TEST_ALERT_TYPE(incoming_connection_alert, 66, alert_priority::normal, alert::peer_notification);
	TEST_ALERT_TYPE(add_torrent_alert, 67, alert_priority::critical, alert::status_notification);
	TEST_ALERT_TYPE(state_update_alert, 68, alert_priority::high, alert::status_notification);
#if TORRENT_ABI_VERSION == 1
	TEST_ALERT_TYPE(mmap_cache_alert, 69, alert_priority::normal, alert::error_notification);
#else
	count_alert_types++;
#endif
	TEST_ALERT_TYPE(session_stats_alert, 70, alert_priority::critical, alert::stats_notification);
	count_alert_types++;
	count_alert_types++; // 72 is gone
	TEST_ALERT_TYPE(dht_error_alert, 73, alert_priority::normal, alert::error_notification | alert::dht_notification);
	TEST_ALERT_TYPE(dht_immutable_item_alert, 74, alert_priority::critical, alert::dht_notification);
	TEST_ALERT_TYPE(dht_mutable_item_alert, 75, alert_priority::critical, alert::dht_notification);
	TEST_ALERT_TYPE(dht_put_alert, 76, alert_priority::normal, alert::dht_notification);
	TEST_ALERT_TYPE(i2p_alert, 77, alert_priority::normal, alert::error_notification);
	TEST_ALERT_TYPE(dht_outgoing_get_peers_alert, 78, alert_priority::normal, alert::dht_notification);
	TEST_ALERT_TYPE(log_alert, 79, alert_priority::normal, alert::session_log_notification);
	TEST_ALERT_TYPE(torrent_log_alert, 80, alert_priority::normal, alert::torrent_log_notification);
	TEST_ALERT_TYPE(peer_log_alert, 81, alert_priority::normal, alert::peer_log_notification);
	TEST_ALERT_TYPE(lsd_error_alert, 82, alert_priority::normal, alert::error_notification);
	TEST_ALERT_TYPE(dht_stats_alert, 83, alert_priority::normal, alert::stats_notification);
	TEST_ALERT_TYPE(incoming_request_alert, 84, alert_priority::normal, alert::incoming_request_notification);
	TEST_ALERT_TYPE(dht_log_alert, 85, alert_priority::normal, alert::dht_log_notification);
	TEST_ALERT_TYPE(dht_pkt_alert, 86, alert_priority::normal, alert::dht_log_notification);
	TEST_ALERT_TYPE(dht_get_peers_reply_alert, 87, alert_priority::normal, alert::dht_operation_notification);
	TEST_ALERT_TYPE(dht_direct_response_alert, 88, alert_priority::critical, alert::dht_notification);
	TEST_ALERT_TYPE(picker_log_alert, 89, alert_priority::normal, alert::picker_log_notification);
	TEST_ALERT_TYPE(session_error_alert, 90, alert_priority::normal, alert::error_notification);
	TEST_ALERT_TYPE(dht_live_nodes_alert, 91, alert_priority::normal, alert::dht_notification);
	TEST_ALERT_TYPE(session_stats_header_alert, 92, alert_priority::normal, alert::stats_notification);
	TEST_ALERT_TYPE(dht_sample_infohashes_alert, 93, alert_priority::normal, alert::dht_operation_notification);
	TEST_ALERT_TYPE(block_uploaded_alert, 94, alert_priority::normal, PROGRESS_NOTIFICATION alert::upload_notification);
	TEST_ALERT_TYPE(alerts_dropped_alert, 95, alert_priority::meta, alert::error_notification);

#undef TEST_ALERT_TYPE

	TEST_EQUAL(num_alert_types, 96);
	TEST_EQUAL(num_alert_types, count_alert_types);
}

TORRENT_TEST(dht_get_peers_reply_alert)
{
	alert_manager mgr(1, dht_get_peers_reply_alert::static_category);

	TEST_EQUAL(mgr.should_post<dht_get_peers_reply_alert>(), true);

	sha1_hash const ih = rand_hash();
	tcp::endpoint const ep1 = rand_tcp_ep(rand_v4);
	tcp::endpoint const ep2 = rand_tcp_ep(rand_v4);
	tcp::endpoint const ep3 = rand_tcp_ep(rand_v4);
	tcp::endpoint const ep4 = rand_tcp_ep(rand_v6);
	tcp::endpoint const ep5 = rand_tcp_ep(rand_v6);
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
	udp::endpoint const ep4 = rand_udp_ep(rand_v6);
	udp::endpoint const ep5 = rand_udp_ep(rand_v6);
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

TORRENT_TEST(session_stats_alert)
{
	alert_manager mgr(1, alert::stats_notification);

	std::vector<alert*> alerts;
	counters cnt;

	mgr.emplace_alert<session_stats_header_alert>();
	mgr.emplace_alert<session_stats_alert>(cnt);
	mgr.get_all(alerts);
	TEST_EQUAL(alerts.size(), 2);

	auto const* h = alert_cast<session_stats_header_alert>(alerts[0]);
	TEST_CHECK(h != nullptr);
	TEST_CHECK(h->message().find("session stats header: ") != std::string::npos);

	auto const* v = alert_cast<session_stats_alert>(alerts[1]);
	TEST_CHECK(v != nullptr);
	TEST_CHECK(v->message().find("session stats (") != std::string::npos);
}

TORRENT_TEST(dht_sample_infohashes_alert)
{
	alert_manager mgr(1, dht_sample_infohashes_alert::static_category);

	TEST_EQUAL(mgr.should_post<dht_sample_infohashes_alert>(), true);

	udp::endpoint const endpoint = rand_udp_ep();
	time_duration const interval = seconds(10);
	int const num = 100;
	sha1_hash const h1 = rand_hash();
	sha1_hash const h2 = rand_hash();
	sha1_hash const h3 = rand_hash();
	sha1_hash const h4 = rand_hash();
	sha1_hash const h5 = rand_hash();

	std::vector<sha1_hash> const v = {h1, h2, h3, h4, h5};

	sha1_hash const nh1 = rand_hash();
	sha1_hash const nh2 = rand_hash();
	sha1_hash const nh3 = rand_hash();
	sha1_hash const nh4 = rand_hash();
	sha1_hash const nh5 = rand_hash();
	udp::endpoint const nep1 = rand_udp_ep(rand_v4);
	udp::endpoint const nep2 = rand_udp_ep(rand_v4);
	udp::endpoint const nep3 = rand_udp_ep(rand_v4);
	udp::endpoint const nep4 = rand_udp_ep(rand_v6);
	udp::endpoint const nep5 = rand_udp_ep(rand_v6);
	std::vector<std::pair<sha1_hash, udp::endpoint>> nv;
	nv.emplace_back(nh1, nep1);
	nv.emplace_back(nh2, nep2);
	nv.emplace_back(nh3, nep3);
	nv.emplace_back(nh4, nep4);
	nv.emplace_back(nh5, nep5);

	mgr.emplace_alert<dht_sample_infohashes_alert>(endpoint, interval, num, v, nv);

	auto const* a = alert_cast<dht_sample_infohashes_alert>(mgr.wait_for_alert(seconds(0)));
	TEST_CHECK(a != nullptr);

	TEST_EQUAL(a->endpoint, endpoint);
	TEST_CHECK(a->interval == interval);
	TEST_EQUAL(a->num_infohashes, num);
	TEST_EQUAL(a->num_samples(), 5);
	TEST_CHECK(a->samples() == v);
	TEST_EQUAL(a->num_nodes(), 5);

	auto nodes = a->nodes();
	std::sort(nv.begin(), nv.end());
	std::sort(nodes.begin(), nodes.end());
	TEST_CHECK(nv == nodes);
}

#undef PROGRESS_NOTIFICATION

