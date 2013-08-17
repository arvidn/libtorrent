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

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/file.hpp"
#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <fstream>
#include <iostream>

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/error.hpp> // for asio::error::get_ssl_category()
#endif

using namespace libtorrent;
using boost::tuples::ignore;

int const alert_mask = alert::all_categories
& ~alert::progress_notification
& ~alert::stats_notification;

struct test_config_t
{
	char const* name;
	bool use_ssl_ports;
	bool seed_has_cert;
	bool downloader_has_cert;
	bool expected_to_complete;
	int peer_errors;
	int ssl_disconnects;
};

test_config_t test_config[] =
{
	{"nobody has a cert (connect to regular port)", false, false, false, false, 0, 0},
	{"nobody has a cert (connect to ssl port)", true, false, false, false, 1, 1},
	{"seed has a cert, but not downloader (connect to regular port)", false, true, false, false, 0, 0},
	{"seed has a cert, but not downloader (connect to ssl port)", true, true, false, false, 1, 1},
	{"downloader has a cert, but not seed (connect to regular port)", false, false, true, false, 0, 0},
	{"downloader has a cert, but not seed (connect to ssl port)", true, false, true, false, 1, 1},
	{"both downloader and seed has a cert (connect to regular port)", false, true, true, false, 0, 0},
#ifdef TORRENT_USE_OPENSSL
	{"both downloader and seed has a cert (connect to ssl port)", true, true, true, true, 0, 0},
#else
	{"both downloader and seed has a cert (connect to ssl port)", true, true, true, false, 0, 0},
#endif
};

int peer_disconnects = 0;
int peer_errors = 0;
int ssl_peer_disconnects = 0;

bool on_alert(alert* a)
{
	if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;
	if (peer_error_alert* e = alert_cast<peer_error_alert>(a))
	{
		++peer_disconnects;
		++peer_errors;

#ifdef TORRENT_USE_OPENSSL
		if (e->error.category() == boost::asio::error::get_ssl_category())
			++ssl_peer_disconnects;
#endif
	}
	return false;
}

void test_ssl(int test_idx)
{
	test_config_t const& test = test_config[test_idx];

	fprintf(stderr, "\n%s TEST: %s\n\n", time_now_string(), test.name);

#ifndef TORRENT_USE_OPENSSL
	if (test.use_ssl_ports)
	{
		fprintf(stderr, "N/A\n");
		return;
	}
#endif

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_ssl", ec);
	remove_all("tmp2_ssl", ec);

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000), "0.0.0.0", 0, alert_mask);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000), "0.0.0.0", 0, alert_mask);

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	session_settings sett;

	sett.ssl_listen = 1024 + rand() % 50000;

	ses1.set_settings(sett);
	sett.ssl_listen += 10;
	ses2.set_settings(sett);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_ssl", ec);
	std::ofstream file("tmp1_ssl/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false, "../ssl/root_ca_cert.pem");
	file.close();

	add_torrent_params addp;
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;
	ssl_peer_disconnects = 0;
	peer_errors = 0;

	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_ssl", 16 * 1024, &t, false, NULL, true, test.use_ssl_ports);

	if (test.seed_has_cert)
	{
		tor1.set_ssl_certificate(
			combine_path("..", combine_path("ssl", "peer_certificate.pem"))
			, combine_path("..", combine_path("ssl", "peer_private_key.pem"))
			, combine_path("..", combine_path("ssl", "dhparams.pem"))
			, "test");
	}

	if (test.downloader_has_cert)
	{
		tor2.set_ssl_certificate(
			combine_path("..", combine_path("ssl", "peer_certificate.pem"))
			, combine_path("..", combine_path("ssl", "peer_private_key.pem"))
			, combine_path("..", combine_path("ssl", "dhparams.pem"))
			, "test");
	}

	for (int i = 0; i < 40; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			std::cerr << time_now_string() << " "
				<< "\033[32m" << int(st1.download_payload_rate / 1000.f) << "kB/s "
				<< "\033[33m" << int(st1.upload_payload_rate / 1000.f) << "kB/s "
				<< "\033[0m" << int(st1.progress * 100) << "% "
				<< st1.num_peers
				<< ": "
				<< "\033[32m" << int(st2.download_payload_rate / 1000.f) << "kB/s "
				<< "\033[31m" << int(st2.upload_payload_rate / 1000.f) << "kB/s "
				<< "\033[0m" << int(st2.progress * 100) << "% "
				<< st2.num_peers
				<< " cc: " << st2.connect_candidates
				<< std::endl;
		}

		if (peer_disconnects >= 2) break;

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

		test_sleep(100);
	}

	fprintf(stderr, "peer_errors: %d  expected: %d\n", peer_errors, test.peer_errors);
	TEST_EQUAL(peer_errors, test.peer_errors);
#ifdef TORRENT_USE_OPENSSL
	fprintf(stderr, "ssl_disconnects: %d  expected: %d\n", ssl_peer_disconnects, test.ssl_disconnects);
	TEST_EQUAL(ssl_peer_disconnects, test.ssl_disconnects);
#endif
	fprintf(stderr, "%s: EXPECT: %s\n", time_now_string(), test.expected_to_complete ? "SUCCEESS" : "FAILURE");
	fprintf(stderr, "%s: RESULT: %s\n", time_now_string(), tor2.status().is_seeding ? "SUCCEESS" : "FAILURE");
	TEST_CHECK(tor2.status().is_seeding == test.expected_to_complete);
}

int test_main()
{
	using namespace libtorrent;

	for (int i = 0; i < sizeof(test_config)/sizeof(test_config[0]); ++i)
		test_ssl(i);
	
	error_code ec;
	remove_all("tmp1_ssl", ec);
	remove_all("tmp2_ssl", ec);

	return 0;
}



