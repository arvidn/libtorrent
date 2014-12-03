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
#include "libtorrent/alert.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"

#include <fstream>

using namespace libtorrent;

int test_main()
{
	int http_port = start_web_server();
	int udp_port = start_tracker();

	int prev_udp_announces = g_udp_tracker_requests;
	int prev_http_announces = g_http_tracker_requests;

	int const alert_mask = alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification;

	session* s = new libtorrent::session(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48875, 49800), "0.0.0.0", 0, alert_mask);

	session_settings sett;
	sett.half_open_limit = 1;
	sett.announce_to_all_trackers = true;
	sett.announce_to_all_tiers = true;
	s->set_settings(sett);

	error_code ec;
	create_directory("tmp1_tracker", ec);
	std::ofstream file(combine_path("tmp1_tracker", "temporary").c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	char tracker_url[200];
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce", http_port);
	t->add_tracker(tracker_url, 0);

	snprintf(tracker_url, sizeof(tracker_url), "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(tracker_url, 1);

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.ti = t;
	addp.save_path = "tmp1_tracker";
	torrent_handle h = s->add_torrent(addp);

	for (int i = 0; i < 100; ++i)
	{
		print_alerts(*s, "s");
		test_sleep(100);
		if (g_udp_tracker_requests == prev_udp_announces + 1
			&& g_http_tracker_requests == prev_http_announces + 1) break;
		fprintf(stderr, "UDP: %d / %d\n", int(g_udp_tracker_requests)
			, int(prev_udp_announces) + 1);
	}

	// we should have announced to the tracker by now
	TEST_EQUAL(g_udp_tracker_requests, prev_udp_announces + 1);
	TEST_EQUAL(g_http_tracker_requests, prev_http_announces + 1);

	fprintf(stderr, "destructing session\n");
	delete s;
	fprintf(stderr, "done\n");

	// we should have announced the stopped event now
	TEST_EQUAL(g_udp_tracker_requests, prev_udp_announces + 2);
	TEST_EQUAL(g_http_tracker_requests, prev_http_announces + 2);

	// ========================================
	// test that we move on to try the next tier if the first one fails
	// ========================================

	s = new libtorrent::session(fingerprint("LT", 0, 1, 0, 0), std::make_pair(39775, 39800), "0.0.0.0", 0, alert_mask);

	sett.half_open_limit = 1;
	sett.announce_to_all_trackers = true;
	sett.announce_to_all_tiers = false;
	sett.tracker_completion_timeout = 2;
	sett.tracker_receive_timeout = 1;
	s->set_settings(sett);

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

	prev_udp_announces = g_udp_tracker_requests;
	prev_http_announces = g_http_tracker_requests;

	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;
	addp.ti = t;
	addp.save_path = "tmp2_tracker";
	h = s->add_torrent(addp);

	for (int i = 0; i < 100; ++i)
	{
		print_alerts(*s, "s");
		test_sleep(100);
		if (g_udp_tracker_requests == prev_udp_announces + 1
			&& g_http_tracker_requests == prev_http_announces) break;
		fprintf(stderr, "UDP: %d / %d\n", int(g_udp_tracker_requests)
			, int(prev_udp_announces) + 1);
	}

	test_sleep(1000);

	TEST_EQUAL(g_udp_tracker_requests, prev_udp_announces + 1);
	TEST_EQUAL(g_http_tracker_requests, prev_http_announces);

	fprintf(stderr, "destructing session\n");
	delete s;
	fprintf(stderr, "done\n");

	fprintf(stderr, "stop_tracker\n");
	stop_tracker();
	fprintf(stderr, "stop_web_server\n");
	stop_web_server();
	fprintf(stderr, "done\n");

	return 0;
}

