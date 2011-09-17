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
	create_directory("./tmp1_tracker", ec);
	std::ofstream file("./tmp1_tracker/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	char tracker_url[200];
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce", http_port);
	t->add_tracker(tracker_url);

	snprintf(tracker_url, sizeof(tracker_url), "udp://127.0.0.1:%d/announce", udp_port);
	t->add_tracker(tracker_url);

	add_torrent_params addp;
	addp.paused = false;
	addp.auto_managed = false;
	addp.ti = t;
	addp.save_path = "./tmp1_tracker";
	torrent_handle h = s->add_torrent(addp);

	test_sleep(2000);

	// we should have announced to the tracker by now
	TEST_EQUAL(g_udp_tracker_requests, prev_udp_announces + 1);
	TEST_EQUAL(g_http_tracker_requests, prev_http_announces + 1);

	delete s;

	// we should have announced the stopped event now
	TEST_EQUAL(g_udp_tracker_requests, prev_udp_announces + 2);
	TEST_EQUAL(g_http_tracker_requests, prev_http_announces + 2);

	stop_tracker();
	stop_web_server();
}

