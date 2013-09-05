/*

Copyright (c) 2008, Arvid Norberg
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
#include <fstream>
#include <iostream>

using namespace libtorrent;
using boost::tuples::ignore;

const int mask = alert::all_categories & ~(alert::performance_warning | alert::stats_notification);

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

// test the maximum transfer rate
void test_rate()
{
	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_transfer", ec);
	remove_all("tmp2_transfer", ec);
	remove_all("tmp1_transfer_moved", ec);
	remove_all("tmp2_transfer_moved", ec);

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48575, 49000), "0.0.0.0", 0, mask);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49575, 50000), "0.0.0.0", 0, mask);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_transfer", ec);
	std::ofstream file("tmp1_transfer/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, 4 * 1024 * 1024, 7);
	file.close();

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;

	session_settings sett = high_performance_seed();
	sett.enable_outgoing_utp = true;
	sett.enable_incoming_utp = true;
	sett.enable_outgoing_tcp = false;
	sett.enable_incoming_tcp = false;
	ses1.set_settings(sett);
	ses2.set_settings(sett);

	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_transfer", 0, &t);

	ses1.set_alert_mask(mask);
	ses2.set_alert_mask(mask);

	ptime start = time_now();

	// it shouldn't take more than 7 seconds
	for (int i = 0; i < 70; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
			print_ses_rate(i / 10.f, &st1, &st2);

		if (peer_disconnects >= 1) break;
		if (st2.is_seeding) break;
		test_sleep(100);
	}

	TEST_CHECK(tor2.status().is_seeding);

	time_duration dt = time_now() - start;

	std::cerr << "downloaded " << t->total_size() << " bytes "
		"in " << (total_milliseconds(dt) / 1000.f) << " seconds" << std::endl;
	
	std::cerr << "average download rate: " << (t->total_size() / (std::max)(total_milliseconds(dt), 1))
		<< " kB/s" << std::endl;

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
}

void print_alert(std::auto_ptr<alert>)
{
	std::cout << "ses1 (alert dispatch function): "/* << a.message() */ << std::endl;
}

// simulate a full disk
struct test_storage : default_storage 
{
	test_storage(storage_params const& params)
		: default_storage(params)
  		, m_written(0)
		, m_limit(16 * 1024 * 2)
	{}

	void set_limit(int lim)
	{
		mutex::scoped_lock l(m_mutex);
		m_limit = lim;
	}

	int writev(
		file::iovec_t const* bufs
		, int num_bufs
		, int piece_index
		, int offset
		, int flags
		, storage_error& se)
	{
		mutex::scoped_lock l(m_mutex);
		if (m_written >= m_limit)
		{
			std::cerr << "storage written: " << m_written << " limit: " << m_limit << std::endl;
			error_code ec;
#if BOOST_VERSION == 103500
			ec = error_code(boost::system::posix_error::no_space_on_device, get_posix_category());
#elif BOOST_VERSION > 103500
			ec = error_code(boost::system::errc::no_space_on_device, get_posix_category());
#else
			ec = error_code(ENOSPC, get_posix_category());
#endif
			se.ec = ec;
			return 0;
		}

		for (int i = 0; i < num_bufs; ++i)
			m_written += bufs[i].iov_len;
		l.unlock();
		return default_storage::writev(bufs, num_bufs, piece_index, offset, flags, se);
	}

	virtual ~test_storage() {}

	int m_written;
	int m_limit;
	mutex m_mutex;
};

storage_interface* test_storage_constructor(storage_params const& params)
{
	return new test_storage(params);
}

void test_transfer(int proxy_type, settings_pack const& sett, bool test_disk_full = false
	, bool test_priorities = false)
{

	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	fprintf(stderr, "\n\n  ==== TESTING %s proxy ==== disk-full: %s allow-fast: %s priorities: %s\n\n\n"
		, test_name[proxy_type], test_disk_full ? "true": "false"
		, test_priorities ? "true" : "false");
	
	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_transfer", ec);
	remove_all("tmp2_transfer", ec);
	remove_all("tmp1_transfer_moved", ec);
	remove_all("tmp2_transfer_moved", ec);

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000), "0.0.0.0", 0, mask);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000), "0.0.0.0", 0, mask);

	proxy_settings ps;
	if (proxy_type)
	{
		ps.port = start_proxy(proxy_type);
		ps.hostname = "127.0.0.1";
		ps.username = "testuser";
		ps.password = "testpass";
		ps.type = (proxy_settings::proxy_type)proxy_type;
		ses1.set_proxy(ps);
		ses2.set_proxy(ps);
	}

	settings_pack pack = sett;
	pack.set_bool(settings_pack::allow_multiple_connections_per_ip, false);

	pack.set_int(settings_pack::unchoke_slots_limit, 0);
	ses1.apply_settings(pack);
	TEST_CHECK(ses1.get_settings().get_int(settings_pack::unchoke_slots_limit) == 0);

	pack.set_int(settings_pack::unchoke_slots_limit, -1);
	ses1.apply_settings(pack);
	TEST_CHECK(ses1.get_settings().get_int(settings_pack::unchoke_slots_limit) == -1);

	pack.set_int(settings_pack::unchoke_slots_limit, 8);
	ses1.apply_settings(pack);
	TEST_CHECK(ses1.get_settings().get_int(settings_pack::unchoke_slots_limit) == 8);

	// we need a short reconnect time since we
	// finish the torrent and then restart it
	// immediately to complete the second half.
	// using a reconnect time > 0 will just add
	// to the time it will take to complete the test
	pack.set_int(settings_pack::min_reconnect_time, 0);
	pack.set_int(settings_pack::stop_tracker_timeout, 1);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);

	// make sure we announce to both http and udp trackers
	pack.set_bool(settings_pack::prefer_udp_trackers, false);
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);

	ses1.apply_settings(pack);
	ses2.apply_settings(pack);

#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::disabled;
	pes.in_enc_policy = pe_settings::disabled;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
#endif

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_transfer", ec);
	std::ofstream file("tmp1_transfer/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	if (test_priorities)
	{
		int udp_tracker_port = start_tracker();
		int tracker_port = start_web_server();

		char tracker_url[200];
		snprintf(tracker_url, sizeof(tracker_url), "http://127.0.0.1:%d/announce", tracker_port);
		t->add_tracker(tracker_url);

		snprintf(tracker_url, sizeof(tracker_url), "udp://127.0.0.1:%d/announce", udp_tracker_port);
		t->add_tracker(tracker_url);
	}

	add_torrent_params addp(&test_storage_constructor);
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses1");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_transfer", 8 * 1024, &t, false, test_disk_full?&addp:0);

	int num_pieces = tor2.torrent_file()->num_pieces();
	std::vector<int> priorities(num_pieces, 1);
	if (test_priorities)
	{
		// set half of the pieces to priority 0
		std::fill(priorities.begin(), priorities.begin() + (num_pieces / 2), 0);
		tor2.prioritize_pieces(priorities);
		std::cerr << "setting priorities: ";
		std::copy(priorities.begin(), priorities.end(), std::ostream_iterator<int>(std::cerr, ", "));
		std::cerr << std::endl;
	}

	ses1.set_alert_mask(mask);
	ses2.set_alert_mask(mask);
//	ses1.set_alert_dispatch(&print_alert);

	// also test to move the storage of the downloader and the uploader
	// to make sure it can handle switching paths
	bool test_move_storage = false;

	tracker_responses = 0;
	int upload_mode_timer = 0;

	wait_for_downloading(ses2, "ses2");

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

		if (!test_move_storage && st2.progress > 0.25f)
		{
			test_move_storage = true;
			tor1.move_storage("tmp1_transfer_moved");
			tor2.move_storage("tmp2_transfer_moved");
			std::cerr << "moving storage" << std::endl;
		}

		// wait 10 loops before we restart the torrent. This lets
		// us catch all events that failed (and would put the torrent
		// back into upload mode) before we restart it.
		if (test_disk_full && st2.upload_mode && ++upload_mode_timer > 10)
		{
			test_disk_full = false;
			((test_storage*)tor2.get_storage_impl())->set_limit(16 * 1024 * 1024);
			tor2.set_upload_mode(false);

			// at this point we probably disconnected the seed
			// so we need to reconnect as well
			fprintf(stderr, "%s: reconnecting peer\n", time_now_string());
			error_code ec;
			tor2.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
				, ses1.listen_port()));

			TEST_CHECK(tor2.status().is_finished == false);
			TEST_EQUAL(peer_disconnects, 2);
			fprintf(stderr, "%s: discovered disk full mode. Raise limit and disable upload-mode\n", time_now_string());
			peer_disconnects = -1;
			continue;
		}

		if (!test_disk_full && st2.is_finished) break;

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data
			|| (test_disk_full && !st2.error.empty()));

		if (peer_disconnects >= 2) break;

		// if nothing is being transferred after 2 seconds, we're failing the test
		if (st1.upload_payload_rate == 0 && i > 20) break;

		test_sleep(100);
	}

	if (test_priorities)
	{
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

		for (int i = 0; i < 50; ++i)
		{
			print_alerts(ses2, "ses2", true, true, true, &on_alert);

			torrent_status st2 = tor2.status();
			if (i % 10 == 0)
			{
				std::cerr << int(st2.progress * 100) << "% " << std::endl;
			}
			if (st2.state != torrent_status::checking_files) break;
			if (peer_disconnects >= 1) break;
			test_sleep(100);
		}

		priorities2 = tor2.piece_priorities();
		std::copy(priorities2.begin(), priorities2.end(), std::ostream_iterator<int>(std::cerr, ", "));
		std::cerr << std::endl;
		TEST_CHECK(std::equal(priorities.begin(), priorities.end(), priorities2.begin()));

		peer_disconnects = 0;

		for (int i = 0; i < 5; ++i)
		{
			print_alerts(ses2, "ses2", true, true, true, &on_alert);
			torrent_status st2 = tor2.status();
			TEST_CHECK(st2.state == torrent_status::finished);
			if (peer_disconnects >= 1) break;
			test_sleep(100);
		}

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
		ptime start = time_now_hires();
		while (a)
		{
			std::auto_ptr<alert> holder = ses2.pop_alert();
			std::cerr << "ses2: " << a->message() << std::endl;
			if (alert_cast<save_resume_data_alert>(a))
			{
				bencode(std::back_inserter(resume_data)
						, *alert_cast<save_resume_data_alert>(a)->resume_data);
				fprintf(stderr, "saved resume data\n");
				break;
			}
			else if (alert_cast<save_resume_data_failed_alert>(a))
			{
				fprintf(stderr, "save resume failed\n");
				break;
			}
			if (total_seconds(time_now_hires() - start) > 10)
				break;

			a = ses2.wait_for_alert(seconds(10));
		}
		TEST_CHECK(resume_data.size());	

		ses2.remove_torrent(tor2);

		std::cerr << "removed" << std::endl;

		test_sleep(100);

		std::cout << "re-adding" << std::endl;
		add_torrent_params p;
		p.flags &= ~add_torrent_params::flag_paused;
		p.flags &= ~add_torrent_params::flag_auto_managed;
		p.ti = t;
		p.save_path = "tmp2_transfer_moved";
		p.resume_data = resume_data;
		tor2 = ses2.add_torrent(p, ec);
		ses2.set_alert_mask(mask);
		tor2.prioritize_pieces(priorities);
		std::cout << "resetting priorities" << std::endl;
		tor2.resume();

		tr = tor2.trackers();
		TEST_CHECK(std::find_if(tr.begin(), tr.end()
			, boost::bind(&announce_entry::url, _1) == "http://test.com/announce") != tr.end());

		peer_disconnects = 0;

		for (int i = 0; i < 5; ++i)
		{
			print_alerts(ses1, "ses1", true, true, true, &on_alert);
			print_alerts(ses2, "ses2", true, true, true, &on_alert);

			torrent_status st1 = tor1.status();
			torrent_status st2 = tor2.status();

			TEST_CHECK(st1.state == torrent_status::seeding);
			TEST_CHECK(st2.state == torrent_status::finished);

			if (peer_disconnects >= 1) break;

			if (st2.is_finished) break;

			test_sleep(100);
		}

		TEST_CHECK(!tor2.status().is_seeding);

		std::fill(priorities.begin(), priorities.end(), 1);
		tor2.prioritize_pieces(priorities);
		std::cout << "setting priorities to 1" << std::endl;
		TEST_EQUAL(tor2.status().is_finished, false);

		peer_disconnects = 0;

		for (int i = 0; i < 130; ++i)
		{
			print_alerts(ses1, "ses1", true, true, true, &on_alert);
			print_alerts(ses2, "ses2", true, true, true, &on_alert);

			torrent_status st1 = tor1.status();
			torrent_status st2 = tor2.status();

			if (i % 10 == 0)
				print_ses_rate(i / 10.f, &st1, &st2);

			if (st2.is_seeding) break;

			TEST_EQUAL(st1.state, torrent_status::seeding);
			TEST_EQUAL(st2.state, torrent_status::downloading);

			if (peer_disconnects >= 1) break;

			test_sleep(100);
		}
	}

	TEST_CHECK(tor2.status().is_seeding);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();

	if (test_priorities)
	{
		stop_tracker();
		stop_web_server();
	}
	if (proxy_type) stop_proxy(ps.port);

}

int test_main()
{
	using namespace libtorrent;

#if !defined TORRENT_DEBUG \
	&& defined TORRENT_DISABLE_INVARIANT_CHECKS \
	&& !defined _GLIBCXX_DEBUG
	// test rate only makes sense in release mode
	test_rate();
	return 0;
#endif

	settings_pack p;

	// test no contiguous_recv_buffers
	p = settings_pack();
	p.set_bool(settings_pack::contiguous_recv_buffer, false);
	test_transfer(0, p);

	// test with all kinds of proxies
	p = settings_pack();
	for (int i = 0; i < 6; ++i)
		test_transfer(i, p);

	// test with a (simulated) full disk
	test_transfer(0, p, true);

	// test allowed fast
	p = settings_pack();
	p.set_int(settings_pack::allowed_fast_set_size, 2000);
	test_transfer(0, p, false, true);

	error_code ec;
	remove_all("tmp1_transfer", ec);
	remove_all("tmp2_transfer", ec);
	remove_all("tmp1_transfer_moved", ec);
	remove_all("tmp2_transfer_moved", ec);

	return 0;
}

