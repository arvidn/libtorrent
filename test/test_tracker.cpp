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
#include "libtorrent/alert.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp" // for parse_tracker_response

#include <fstream>

using namespace libtorrent;
namespace lt = libtorrent;

void test_parse_hostname_peers()
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

void test_parse_peers4()
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

void test_parse_interval()
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

void test_parse_warning()
{
	char const response[] = "d5:peers0:15:warning message12:test messagee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code());
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.warning_message, "test message");
}

void test_parse_failure_reason()
{
	char const response[] = "d5:peers0:14:failure reason12:test messagee";
	error_code ec;
	tracker_response resp = parse_tracker_response(response, sizeof(response) - 1
		, ec, false, sha1_hash());

	TEST_EQUAL(ec, error_code(errors::tracker_failure));
	TEST_EQUAL(resp.peers.size(), 0);
	TEST_EQUAL(resp.failure_reason, "test message");
}

void test_parse_scrape_response()
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

void test_parse_scrape_response_with_zero()
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

void test_parse_external_ip()
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
void test_parse_external_ip6()
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

int test_main()
{
	test_parse_hostname_peers();
	test_parse_peers4();
	test_parse_interval();
	test_parse_warning();
	test_parse_failure_reason();
	test_parse_scrape_response();
	test_parse_scrape_response_with_zero();
	test_parse_external_ip();
#if TORRENT_USE_IPV6
	test_parse_external_ip6();
#endif

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

	int http_port = start_web_server();
	int udp_port = start_udp_tracker();

	int prev_udp_announces = num_udp_announces();

	int const alert_mask = alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification;

	lt::session* s = new lt::session(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48875, 49800), "0.0.0.0", 0, alert_mask);

	settings_pack pack;
#ifndef TORRENT_NO_DEPRECATE
	pack.set_int(settings_pack::half_open_limit, 1);
#endif
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	s->apply_settings(pack);

	error_code ec;
	remove_all("tmp1_tracker", ec);
	create_directory("tmp1_tracker", ec);
	std::ofstream file(combine_path("tmp1_tracker", "temporary").c_str());
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
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

	fprintf(stderr, "destructing session\n");
	delete s;
	fprintf(stderr, "done\n");

	// we should have announced the stopped event now
	TEST_EQUAL(num_udp_announces(), prev_udp_announces + 2);

	// ========================================
	// test that we move on to try the next tier if the first one fails
	// ========================================

	s = new lt::session(fingerprint("LT", 0, 1, 0, 0), std::make_pair(39775, 39800), "0.0.0.0", 0, alert_mask);

	pack.clear();
#ifndef TORRENT_NO_DEPRECATE
	pack.set_int(settings_pack::half_open_limit, 1);
#endif
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_int(settings_pack::tracker_completion_timeout, 2);
	pack.set_int(settings_pack::tracker_receive_timeout, 1);
	s->apply_settings(pack);

	remove_all("tmp2_tracker", ec);
	create_directory("tmp2_tracker", ec);
	file.open(combine_path("tmp2_tracker", "temporary").c_str());
	t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	// this should fail
	snprintf(tracker_url, sizeof(tracker_url), "udp://www1.non-existent.com:80/announce");
	t->add_tracker(tracker_url, 0);

	// and this should fail
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.2:3/announce");
	t->add_tracker(tracker_url, 1);

	// this should be announced to
	// udp trackers are prioritized if they're on the same host as an http one
	// so this must be before the http one on 127.0.0.1
	snprintf(tracker_url, sizeof(tracker_url), "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(tracker_url, 2);

	// and this should not be announced to (since the one before it succeeded)
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce", http_port);
	t->add_tracker(tracker_url, 3);

	prev_udp_announces = num_udp_announces();

	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.flags |= add_torrent_params::flag_seed_mode;
	addp.ti = t;
	addp.save_path = "tmp2_tracker";
	h = s->add_torrent(addp);

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(*s, "s");
		if (num_udp_announces() == prev_udp_announces + 1) break;

		fprintf(stderr, "UDP: %d / %d\n", int(num_udp_announces())
			, int(prev_udp_announces) + 1);
		test_sleep(100);
	}

	test_sleep(1000);

	TEST_EQUAL(num_udp_announces(), prev_udp_announces + 1);

	fprintf(stderr, "destructing session\n");
	delete s;
	fprintf(stderr, "done\n");

	fprintf(stderr, "stop_tracker\n");
	stop_udp_tracker();
	fprintf(stderr, "stop_web_server\n");
	stop_web_server();
	fprintf(stderr, "done\n");

	return 0;
}

