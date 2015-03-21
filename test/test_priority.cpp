/*

Copyright (c) 2008-2013, Arvid Norberg
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

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/file.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "udp_tracker.hpp"
#include <fstream>
#include <iostream>

using namespace libtorrent;
using boost::tuples::ignore;

int const alert_mask = alert::all_categories
& ~alert::progress_notification
& ~alert::stats_notification;

int peer_disconnects = 0;

int tracker_responses = 0;

bool on_alert(alert* a)
{
	if (alert_cast<tracker_reply_alert>(a))
		++tracker_responses;
	else if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;
	else if (alert_cast<peer_error_alert>(a))
		++peer_disconnects;

	return false;
}

int udp_tracker_port;
int tracker_port;

// these are declared before the session objects
// so that they are destructed last. This enables
// the sessions to destruct in parallel
std::vector<session_proxy> sp;

void test_transfer(session_settings& sett)
{
	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_priority", ec);
	remove_all("tmp2_priority", ec);
	remove_all("tmp1_priority_moved", ec);
	remove_all("tmp2_priority_moved", ec);

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000), "0.0.0.0", 0, alert_mask);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000), "0.0.0.0", 0, alert_mask);

	// we need a short reconnect time since we
	// finish the torrent and then restart it
	// immediately to complete the second half.
	// using a reconnect time > 0 will just add
	// to the time it will take to complete the test
	sett.min_reconnect_time = 0;
	sett.stop_tracker_timeout = 1;
	sett.announce_to_all_trackers = true;
	sett.announce_to_all_tiers = true;
	// make sure we announce to both http and udp trackers
	sett.prefer_udp_trackers = false;
	sett.enable_outgoing_utp = false;
	sett.enable_incoming_utp = false;

	ses1.set_settings(sett);
	ses2.set_settings(sett);

#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::disabled;
	pes.in_enc_policy = pe_settings::disabled;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
#endif

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_priority", ec);
	std::ofstream file("tmp1_priority/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	char tracker_url[200];
	snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce", tracker_port);
	t->add_tracker(tracker_url);

	snprintf(tracker_url, sizeof(tracker_url), "udp://127.0.0.1:%d/announce", udp_tracker_port);
	t->add_tracker(tracker_url);

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses1");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_priority", 8 * 1024, &t, false, 0);

	int num_pieces = tor2.torrent_file()->num_pieces();
	std::vector<int> priorities(num_pieces, 1);
	// set half of the pieces to priority 0
	std::fill(priorities.begin(), priorities.begin() + (num_pieces / 2), 0);
	tor2.prioritize_pieces(priorities);
	std::cerr << "setting priorities: ";
	std::copy(priorities.begin(), priorities.end(), std::ostream_iterator<int>(std::cerr, ", "));
	std::cerr << std::endl;

	tracker_responses = 0;

	for (int i = 0; i < 200; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, &st1, &st2);
		}

		// st2 is finished when we have downloaded half of the pieces
		if (st2.is_finished) break;

		if (st2.state != torrent_status::downloading)
		{
			static char const* state_str[] =	
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		if (peer_disconnects >= 2) break;

		// if nothing is being transferred after 2 seconds, we're failing the test
		if (st1.upload_payload_rate == 0 && i > 20) break;

		test_sleep(100);
	}

	// 1 announce per tracker to start
	TEST_CHECK(tracker_responses >= 2);

	TEST_CHECK(!tor2.status().is_seeding);
	TEST_CHECK(tor2.status().is_finished);

	if (tor2.status().is_finished)
		std::cerr << "torrent is finished (50% complete)" << std::endl;
	else return;

	std::vector<int> priorities2 = tor2.piece_priorities();
	std::copy(priorities2.begin(), priorities2.end(), std::ostream_iterator<int>(std::cerr, ", "));
	std::cerr << std::endl;
	TEST_CHECK(std::equal(priorities.begin(), priorities.end(), priorities2.begin()));

	std::cerr << "force recheck" << std::endl;
	tor2.force_recheck();

	priorities2 = tor2.piece_priorities();
	std::copy(priorities2.begin(), priorities2.end(), std::ostream_iterator<int>(std::cerr, ", "));
	std::cerr << std::endl;
	TEST_CHECK(std::equal(priorities.begin(), priorities.end(), priorities2.begin()));

	peer_disconnects = 0;

	// wait until force-recheck is complete
	// when we're done checking, we're likely to be put in downloading state
	// for a split second before transitioning to finished. This loop waits
	// for the finished state
	torrent_status st2;
	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		st2 = tor2.status();
		if (i % 10 == 0)
		{
			std::cerr << int(st2.progress * 100) << "% " << std::endl;
		}
		if (st2.state == torrent_status::finished) break;
		test_sleep(100);
	}

	TEST_CHECK(st2.state != torrent_status::checking_files);
	if (st2.state != torrent_status::checking_files) std::cerr << "recheck complete" << std::endl;

	priorities2 = tor2.piece_priorities();
	std::copy(priorities2.begin(), priorities2.end(), std::ostream_iterator<int>(std::cerr, ", "));
	std::cerr << std::endl;
	TEST_CHECK(std::equal(priorities.begin(), priorities.end(), priorities2.begin()));

	tor2.pause();
	alert const* a = ses2.wait_for_alert(seconds(10));
	bool got_paused_alert = false;
	while (a)
	{
		std::auto_ptr<alert> holder = ses2.pop_alert();
		std::cerr << "ses2: " << a->message() << std::endl;
		if (alert_cast<torrent_paused_alert>(a))
		{
			got_paused_alert = true;
			break;	
		}
		a = ses2.wait_for_alert(seconds(10));
	}
	TEST_CHECK(got_paused_alert);	

	std::vector<announce_entry> tr = tor2.trackers();
	tr.push_back(announce_entry("http://test.com/announce"));
	tor2.replace_trackers(tr);
	tr.clear();

	tor2.save_resume_data();

	std::vector<char> resume_data;
	a = ses2.wait_for_alert(seconds(10));
	while (a)
	{
		std::auto_ptr<alert> holder = ses2.pop_alert();
		std::cerr << "ses2: " << a->message() << std::endl;
		if (alert_cast<save_resume_data_alert>(a))
		{
			bencode(std::back_inserter(resume_data)
					, *alert_cast<save_resume_data_alert>(a)->resume_data);
			break;
		}
		a = ses2.wait_for_alert(seconds(10));
	}
	TEST_CHECK(resume_data.size());	

	std::cerr << "saved resume data" << std::endl;

	ses2.remove_torrent(tor2);

	std::cerr << "removed" << std::endl;

	test_sleep(100);

	std::cout << "re-adding" << std::endl;
	add_torrent_params p;
	p.flags &= ~add_torrent_params::flag_paused;
	p.flags &= ~add_torrent_params::flag_auto_managed;
	p.ti = t;
	p.save_path = "tmp2_priority";
	p.resume_data = resume_data;
	tor2 = ses2.add_torrent(p, ec);
	ses2.set_alert_mask(alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification);
	tor2.prioritize_pieces(priorities);
	std::cout << "resetting priorities" << std::endl;
	tor2.resume();

	tr = tor2.trackers();
	TEST_CHECK(std::find_if(tr.begin(), tr.end()
		, boost::bind(&announce_entry::url, _1) == "http://test.com/announce") != tr.end());

	// wait for the files in ses2 to be checked, i.e. the torrent
	// to turn into finished state
	for (int i = 0; i < 5; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		TEST_EQUAL(st1.state, torrent_status::seeding);

		if (st2.is_finished) break;

		test_sleep(100);
	}

	TEST_CHECK(!tor2.status().is_seeding);

	std::fill(priorities.begin(), priorities.end(), 1);
	tor2.prioritize_pieces(priorities);
	std::cout << "setting priorities to 1" << std::endl;
	TEST_EQUAL(tor2.status().is_finished, false);

	std::copy(priorities.begin(), priorities.end(), std::ostream_iterator<int>(std::cerr, ", "));
	std::cerr << std::endl;

	// drain alerts
	print_alerts(ses1, "ses1", true, true, true, &on_alert);
	print_alerts(ses2, "ses2", true, true, true, &on_alert);

	peer_disconnects = 0;

	// this loop makes sure ses2 reconnects to the peer now that it's
	// in download mode again. If this fails, the reconnect logic may
	// not work or be inefficient
	torrent_status st1 = tor1.status();
	st2 = tor2.status();
	for (int i = 0; i < 130; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		st1 = tor1.status();
		st2 = tor2.status();

		if (i % 10 == 0)
			print_ses_rate(i / 10.f, &st1, &st2);

		if (st2.is_seeding) break;

		TEST_EQUAL(st1.state, torrent_status::seeding);
		TEST_EQUAL(st2.state, torrent_status::downloading);

		if (peer_disconnects >= 2)
		{
			fprintf(stderr, "too many disconnects (%d), exiting\n", peer_disconnects);
			break;
		}

		test_sleep(100);
	}

	st2 = tor2.status();
	if (!st2.is_seeding)
		fprintf(stderr, "ses2 failed to reconnect to ses1!\n");
	TEST_CHECK(st2.is_seeding);

	// this allows shutting down the sessions in parallel
	sp.push_back(ses1.abort());
	sp.push_back(ses2.abort());
}

int test_main()
{
	using namespace libtorrent;

	udp_tracker_port = start_udp_tracker();
	tracker_port = start_web_server();

	session_settings sett;
	sett.allow_multiple_connections_per_ip = false;
	sett.ignore_limits_on_local_network = false;
	sett.unchoke_slots_limit = 8;

	test_transfer(sett);

	sett.lazy_bitfields = true;

	test_transfer(sett);
	
	error_code ec;
	remove_all("tmp1_priorities", ec);
	remove_all("tmp2_priorities", ec);
	remove_all("tmp1_priorities_moved", ec);
	remove_all("tmp2_priorities_moved", ec);

	stop_udp_tracker();
	stop_web_server();

	// we have to clear them, session doesn't really support being destructed
	// as a global destructor (for silly reasons)
	sp.clear();

	return 0;
}


