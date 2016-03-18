/*

Copyright (c) 2010, Arvid Norberg
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
#include "udp_tracker.hpp"
#include "settings.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/peer_info.hpp" // for peer_list_entry
#include "libtorrent/broadcast_socket.hpp" // for supports_ipv6
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp" // for parse_tracker_response
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"

#include <fstream>

using namespace libtorrent;
namespace lt = libtorrent;

// TODO: test scrape requests
// TODO: test parse peers6
// TODO: test parse tracker-id
// TODO: test parse failure-reason
// TODO: test all failure paths, including
//   invalid bencoding
//   not a dictionary
//   no files entry in scrape response
//   no info-hash entry in scrape response
//   malformed peers in peer list of dictionaries
//   uneven number of bytes in peers and peers6 string responses

TORRENT_TEST(parse_hostname_peers)
{
	char const response[] = "d5:peersld7:peer id20:aaaaaaaaaaaaaaaaaaaa2:ip13:test_hostname4:porti1000eed7:peer id20:bbbbabaababababababa2:ip12:another_host4:porti1001eeee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 2);
	if (resp.peers.size() == 2)
	{
		peer_entry const& e0 = resp.peers[0];
		peer_entry const& e1 = resp.peers[1];
		TEST_EQUAL(e0.hostname, "test_hostname");
		TEST_EQUAL(e0.port, 1000);
		TEST_EQUAL(e0.pid, peer_id("aaaaaaaaaaaaaaaaaaaa"));

		TEST_EQUAL(e1.hostname, "another_host");
		TEST_EQUAL(e1.port, 1001);
		TEST_EQUAL(e1.pid, peer_id("bbbbabaababababababa"));
	}
}

TORRENT_TEST(parse_peers4)
{
	char const response[] = "d5:peers12:\x01\x02\x03\x04\x30\x10"
		"\x09\x08\x07\x06\x20\x10" "e";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers4.size(), 2);
	if (resp.peers.size() == 2)
	{
		ipv4_peer_entry const& e0 = resp.peers4[0];
		ipv4_peer_entry const& e1 = resp.peers4[1];
		TEST_CHECK(e0.ip == address_v4::from_string("1.2.3.4").to_bytes());
		TEST_EQUAL(e0.port, 0x3010);

		TEST_CHECK(e1.ip == address_v4::from_string("9.8.7.6").to_bytes());
		TEST_EQUAL(e1.port, 0x2010);
	}
}

TORRENT_TEST(parse_i2p_peers)
{
	// d8:completei8e10:incompletei4e8:intervali3600e5:peers352: ...
	boost::uint8_t const response[] = { 0x64, 0x38, 0x3a, 0x63, 0x6f, 0x6d,
		0x70, 0x6c, 0x65, 0x74, 0x65, 0x69, 0x38, 0x65, 0x31, 0x30,
		0x3a, 0x69, 0x6e, 0x63, 0x6f, 0x6d, 0x70, 0x6c, 0x65, 0x74,
		0x65, 0x69, 0x34, 0x65, 0x38, 0x3a, 0x69, 0x6e, 0x74, 0x65,
		0x72, 0x76, 0x61, 0x6c, 0x69, 0x33, 0x36, 0x30, 0x30, 0x65,
		0x35, 0x3a, 0x70, 0x65, 0x65, 0x72, 0x73, 0x33, 0x35, 0x32,
		0x3a, 0xb1, 0x84, 0xe0, 0x96, 0x1f, 0xdb, 0xf2, 0xc9, 0xb0,
		0x53, 0x9a, 0x31, 0xa5, 0x35, 0xcd, 0xe8, 0x59, 0xa0, 0x7c,
		0xcd, 0xf2, 0x7c, 0x81, 0x81, 0x02, 0x11, 0x7b, 0xb4, 0x2a,
		0xd1, 0x20, 0x87, 0xd6, 0x1b, 0x06, 0x4c, 0xbb, 0x4c, 0x4e,
		0x30, 0xf9, 0xa3, 0x5d, 0x58, 0xa0, 0xa5, 0x10, 0x48, 0xfa,
		0x9b, 0x3b, 0x10, 0x86, 0x43, 0x5c, 0x2e, 0xa2, 0xa6, 0x22,
		0x31, 0xd0, 0x63, 0x6a, 0xfb, 0x4f, 0x25, 0x5b, 0xe2, 0x29,
		0xbc, 0xcc, 0xa0, 0x1a, 0x0a, 0x30, 0x45, 0x32, 0xa1, 0xc8,
		0x49, 0xf7, 0x9e, 0x03, 0xfd, 0x34, 0x80, 0x9a, 0x5b, 0xe9,
		0x78, 0x04, 0x48, 0x4e, 0xbd, 0xc0, 0x5c, 0xdd, 0x4f, 0xf8,
		0xbd, 0xc8, 0x4c, 0x4b, 0xcc, 0xf6, 0x25, 0x1b, 0xb3, 0x4d,
		0xc0, 0x91, 0xb1, 0x4b, 0xb6, 0xbd, 0x95, 0xb7, 0x8e, 0x88,
		0x79, 0xa8, 0xaa, 0x83, 0xa5, 0x7e, 0xec, 0x17, 0x60, 0x8d,
		0x1d, 0xe2, 0xbe, 0x16, 0x35, 0x83, 0x25, 0xee, 0xe4, 0xd5,
		0xbe, 0x54, 0x7b, 0xc8, 0x00, 0xdc, 0x5d, 0x56, 0xc7, 0x29,
		0xd2, 0x1e, 0x6d, 0x7a, 0xfb, 0xfc, 0xef, 0x36, 0x05, 0x8a,
		0xd0, 0xa7, 0x05, 0x4c, 0x11, 0xd5, 0x50, 0xe6, 0x2d, 0x7b,
		0xe0, 0x7d, 0x84, 0xda, 0x47, 0x48, 0x9d, 0xf9, 0x77, 0xa2,
		0xc7, 0x78, 0x90, 0xa4, 0xb5, 0x05, 0xf4, 0x95, 0xea, 0x36,
		0x7b, 0x92, 0x8c, 0x5b, 0xf7, 0x8b, 0x18, 0x94, 0x2c, 0x2f,
		0x88, 0xcf, 0xf8, 0xec, 0x5c, 0x52, 0xa8, 0x98, 0x8f, 0xd1,
		0xd3, 0xf0, 0xd8, 0x63, 0x19, 0x73, 0x33, 0xd7, 0xeb, 0x1f,
		0x87, 0x1c, 0x9f, 0x5b, 0xce, 0xe4, 0xd0, 0x15, 0x4e, 0x38,
		0xb7, 0xe3, 0xbd, 0x93, 0x64, 0xe2, 0x15, 0x3d, 0xfc, 0x56,
		0x4f, 0xd4, 0x19, 0x62, 0xe0, 0xb7, 0x59, 0x24, 0xff, 0x7f,
		0x32, 0xdf, 0x56, 0xa5, 0x62, 0x42, 0x87, 0xa3, 0x04, 0xec,
		0x09, 0x0a, 0x5b, 0x90, 0x48, 0x57, 0xc3, 0x32, 0x5f, 0x87,
		0xeb, 0xfb, 0x08, 0x69, 0x6f, 0xa9, 0x46, 0x46, 0xa9, 0x54,
		0x67, 0xec, 0x7b, 0x15, 0xc9, 0x68, 0x6b, 0x01, 0xb8, 0x10,
		0x59, 0x53, 0x9c, 0xe6, 0x1b, 0x2e, 0x70, 0x72, 0x6e, 0x82,
		0x7b, 0x03, 0xbc, 0xf2, 0x26, 0x9b, 0xb3, 0x91, 0xaa, 0xf1,
		0xba, 0x62, 0x12, 0xbb, 0x74, 0x4b, 0x70, 0x44, 0x74, 0x19,
		0xb2, 0xa1, 0x68, 0xd2, 0x30, 0xd6, 0xa5, 0x1b, 0xd9, 0xea,
		0x4d, 0xdb, 0x81, 0x8e, 0x66, 0xbf, 0x4d, 0x6c, 0x32, 0x66,
		0xc2, 0x8a, 0x22, 0x6b, 0x47, 0xc1, 0xd1, 0x52, 0x61, 0x66,
		0xa0, 0x75, 0xab, 0x65 };
	error_code ec;
	tracker_response resp = parse_tracker_response(
		reinterpret_cast<char const*>(response), sizeof(response)
		, ec, tracker_request::i2p, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 11);

	if (resp.peers.size() == 11)
	{
		TEST_EQUAL(resp.peers[0].hostname, "wgcobfq73pzmtmcttiy2knon5bm2a7gn6j6idaiccf53ikwrecdq.b32.i2p");
		TEST_EQUAL(resp.peers[10].hostname, "ufunemgwuun5t2sn3oay4zv7jvwdezwcrirgwr6b2fjgczvaowvq.b32.i2p");
	}
}

TORRENT_TEST(parse_interval)
{
	char const response[] = "d8:intervali1042e12:min intervali10e5:peers0:e";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.peers4.size(), 0);
	TEST_EQUAL(resp.interval, 1042);
	TEST_EQUAL(resp.min_interval, 10);
}

TORRENT_TEST(parse_warning)
{
	char const response[] = "d5:peers0:15:warning message12:test messagee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.warning_message, "test message");
}

TORRENT_TEST(parse_failure_reason)
{
	char const response[] = "d5:peers0:14:failure reason12:test messagee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code(errors::tracker_failure));
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.failure_reason, "test message");
}

TORRENT_TEST(parse_scrape_response)
{
	char const response[] = "d5:filesd20:aaaaaaaaaaaaaaaaaaaad8:completei1e10:incompletei2e10:downloadedi3e11:downloadersi6eeee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, true, sha1_hash("aaaaaaaaaaaaaaaaaaaa"));

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.complete, 1);
	TEST_EQUAL(resp.incomplete, 2);
	TEST_EQUAL(resp.downloaded, 3);
	TEST_EQUAL(resp.downloaders, 6);
}

TORRENT_TEST(parse_scrape_response_with_zero)
{
	char const response[] = "d5:filesd20:aaa\0aaaaaaaaaaaaaaaad8:completei4e10:incompletei5e10:downloadedi6eeee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, true, sha1_hash("aaa\0aaaaaaaaaaaaaaaa"));

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.complete, 4);
	TEST_EQUAL(resp.incomplete, 5);
	TEST_EQUAL(resp.downloaded, 6);
	TEST_EQUAL(resp.downloaders, -1);
}

TORRENT_TEST(parse_external_ip)
{
	char const response[] = "d5:peers0:11:external ip4:\x01\x02\x03\x04" "e";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.external_ip, address_v4::from_string("1.2.3.4"));
}

#if TORRENT_USE_IPV6
TORRENT_TEST(parse_external_ip6)
{
	char const response[] = "d5:peers0:11:external ip16:\xf1\x02\x03\x04\0\0\0\0\0\0\0\0\0\0\xff\xff" "e";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.external_ip, address_v6::from_string("f102:0304::ffff"));
}
#endif

peer_entry extract_peer(char const* peer_field, error_code expected_ec, bool expected_ret)
{
	error_code ec;
	peer_entry result;
	bdecode_node n;
	bdecode(peer_field, peer_field + strlen(peer_field)
		, n, ec, NULL, 1000, 1000);
	TEST_CHECK(!ec);
	bool ret = extract_peer_info(n, result, ec);
	TEST_EQUAL(expected_ret, ret);
	TEST_EQUAL(expected_ec, ec);
	return result;
}

TORRENT_TEST(extract_peer)
{
	peer_entry result = extract_peer("d7:peer id20:abababababababababab2:ip4:abcd4:porti1337ee"
		, error_code(), true);
	TEST_EQUAL(result.hostname, "abcd");
	TEST_EQUAL(result.pid, peer_id("abababababababababab"));
	TEST_EQUAL(result.port, 1337);
}

TORRENT_TEST(extract_peer_hostname)
{
	peer_entry result = extract_peer("d2:ip11:example.com4:porti1ee"
		, error_code(), true);
	TEST_EQUAL(result.hostname, "example.com");
	TEST_EQUAL(result.pid, (peer_id::min)());
	TEST_EQUAL(result.port, 1);
}

TORRENT_TEST(extract_peer_not_a_dictionary)
{
	// not a dictionary
	peer_entry result = extract_peer("2:ip11:example.com"
		, error_code(errors::invalid_peer_dict, get_libtorrent_category()), false);
}

TORRENT_TEST(extract_peer_missing_ip)
{
	// missing IP
	peer_entry result = extract_peer("d7:peer id20:abababababababababab4:porti1337ee"
		, error_code(errors::invalid_tracker_response, get_libtorrent_category()), false);
}

TORRENT_TEST(extract_peer_missing_port)
{
	// missing port
	peer_entry result = extract_peer("d7:peer id20:abababababababababab2:ip4:abcde"
		, error_code(errors::invalid_tracker_response, get_libtorrent_category()), false);
}

TORRENT_TEST(udp_tracker)
{
	int http_port = start_web_server();
	int udp_port = start_udp_tracker();

	int prev_udp_announces = num_udp_announces();

	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48875");

	boost::scoped_ptr<lt::session> s(new lt::session(pack));

	error_code ec;
	remove_all("tmp1_tracker", ec);
	create_directory("tmp1_tracker", ec);
	std::ofstream file(combine_path("tmp1_tracker", "temporary").c_str());
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 16 * 1024, 13, false);
	file.close();

	char tracker_url[200];
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce", http_port);
	t->add_tracker(tracker_url, 0);

	snprintf(tracker_url, sizeof(tracker_url), "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(tracker_url, 1);

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.flags |= add_torrent_params::flag_seed_mode;
	addp.ti = t;
	addp.save_path = "tmp1_tracker";
	torrent_handle h = s->add_torrent(addp);

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(*s, "s");
		if (num_udp_announces() == prev_udp_announces + 1)
			break;

		test_sleep(100);
		fprintf(stderr, "UDP: %d / %d\n", int(num_udp_announces())
			, int(prev_udp_announces) + 1);
	}

	// we should have announced to the tracker by now
	TEST_EQUAL(num_udp_announces(), prev_udp_announces + 1);

	// if we remove the torrent before it has received the response from the
	// tracker, it won't announce again to stop. So, wait a bit before removing.
	test_sleep(1000);

	s->remove_torrent(h);

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(*s, "s", true, true);
		if (num_udp_announces() == prev_udp_announces + 2)
			break;

		test_sleep(100);
		fprintf(stderr, "UDP: %d / %d\n", int(num_udp_announces())
			, int(prev_udp_announces) + 1);
	}

	fprintf(stderr, "destructing session\n");
	s.reset();
	fprintf(stderr, "done\n");

	// we should have announced the stopped event now
	TEST_EQUAL(num_udp_announces(), prev_udp_announces + 2);
}

TORRENT_TEST(http_peers)
{
	int http_port = start_web_server();

	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_int(settings_pack::tracker_completion_timeout, 2);
	pack.set_int(settings_pack::tracker_receive_timeout, 1);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:39775");

	boost::scoped_ptr<lt::session> s(new lt::session(pack));

	error_code ec;
	remove_all("tmp2_tracker", ec);
	create_directory("tmp2_tracker", ec);
	std::ofstream file(combine_path("tmp2_tracker", "temporary").c_str());
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 16 * 1024, 13, false);
	file.close();

	char tracker_url[200];
	// and this should not be announced to (since the one before it succeeded)
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce"
		, http_port);
	t->add_tracker(tracker_url, 0);

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.flags |= add_torrent_params::flag_seed_mode;
	addp.ti = t;
	addp.save_path = "tmp2_tracker";
	torrent_handle h = s->add_torrent(addp);

	// wait to hit the tracker
	wait_for_alert(*s, tracker_reply_alert::alert_type, "s");

	// we expect to have certain peers in our peer list now
	// these peers are hard coded in web_server.py
	std::vector<peer_list_entry> peers;
	h.get_full_peer_list(peers);

	std::set<tcp::endpoint> expected_peers;
	expected_peers.insert(tcp::endpoint(address_v4::from_string("65.65.65.65"), 16962));
	expected_peers.insert(tcp::endpoint(address_v4::from_string("67.67.67.67"), 17476));
#if TORRENT_USE_IPV6
	if (supports_ipv6())
	{
		expected_peers.insert(tcp::endpoint(address_v6::from_string("4545:4545:4545:4545:4545:4545:4545:4545"), 17990));
	}
#endif

	TEST_EQUAL(peers.size(), expected_peers.size());
	for (std::vector<peer_list_entry>::iterator i = peers.begin()
		, end(peers.end()); i != end; ++i)
	{
		TEST_EQUAL(expected_peers.count(i->ip), 1);
	}

	fprintf(stderr, "destructing session\n");
	s.reset();
	fprintf(stderr, "done\n");

	fprintf(stderr, "stop_web_server\n");
	stop_web_server();
	fprintf(stderr, "done\n");
}

void test_proxy(bool proxy_trackers)
{
	int http_port = start_web_server();

	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_int(settings_pack::tracker_completion_timeout, 2);
	pack.set_int(settings_pack::tracker_receive_timeout, 1);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:39775");
	pack.set_bool(settings_pack::force_proxy, true);

	pack.set_str(settings_pack::proxy_hostname, "non-existing.com");
	pack.set_int(settings_pack::proxy_type, settings_pack::socks5);
	pack.set_int(settings_pack::proxy_port, 4444);
	pack.set_bool(settings_pack::proxy_tracker_connections, proxy_trackers);

	boost::scoped_ptr<lt::session> s(new lt::session(pack));

	error_code ec;
	remove_all("tmp2_tracker", ec);
	create_directory("tmp2_tracker", ec);
	std::ofstream file(combine_path("tmp2_tracker", "temporary").c_str());
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 16 * 1024, 13, false);
	file.close();

	char tracker_url[200];
	// and this should not be announced to (since the one before it succeeded)
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce"
		, http_port);
	t->add_tracker(tracker_url, 0);

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.flags |= add_torrent_params::flag_seed_mode;
	addp.ti = t;
	addp.save_path = "tmp2_tracker";
	torrent_handle h = s->add_torrent(addp);

	// wait to hit the tracker
	const alert* a = wait_for_alert(*s, tracker_reply_alert::alert_type, "s");
	if (proxy_trackers)
	{
		TEST_CHECK(a == NULL);
	}
	else
	{
		TEST_CHECK(a != NULL);
	}

	fprintf(stderr, "destructing session\n");
	s.reset();
	fprintf(stderr, "done\n");

	fprintf(stderr, "stop_web_server\n");
	stop_web_server();
	fprintf(stderr, "done\n");
}

TORRENT_TEST(tracker_proxy)
{
	fprintf(stderr, "\n\nnot proxying tracker connections (expect to reach the tracker)\n\n");
	test_proxy(false);

	fprintf(stderr, "\n\nproxying tracker connections through non-existent proxy (do not expect to reach the tracker)\n\n");
	test_proxy(true);
}

