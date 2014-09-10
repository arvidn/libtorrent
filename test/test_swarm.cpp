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
#include "libtorrent/thread.hpp"
#include "libtorrent/time.hpp"
#include <boost/tuple/tuple.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <iostream>

void test_swarm(bool super_seeding = false, bool strict = false, bool seed_mode = false, bool time_critical = false)
{
	using namespace libtorrent;

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_swarm", ec);
	remove_all("tmp2_swarm", ec);
	remove_all("tmp3_swarm", ec);

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;
	session_proxy p3;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48000, 49000), "0.0.0.0", 0);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49000, 50000), "0.0.0.0", 0);
	session ses3(fingerprint("LT", 0, 1, 0, 0), std::make_pair(50000, 51000), "0.0.0.0", 0);

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	float rate_limit = 100000;

	session_settings settings;
	settings.allow_multiple_connections_per_ip = true;
	settings.ignore_limits_on_local_network = false;
	settings.strict_super_seeding = strict;

	settings.upload_rate_limit = rate_limit;
	ses1.set_settings(settings);

	settings.download_rate_limit = rate_limit / 2;
	settings.upload_rate_limit = rate_limit;
	ses2.set_settings(settings);
	ses3.set_settings(settings);

#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::forced;
	pes.in_enc_policy = pe_settings::forced;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
	ses3.set_pe_settings(pes);
#endif

	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;

	add_torrent_params p;
	p.flags |= add_torrent_params::flag_seed_mode;
	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true
		, false, true, "_swarm", 8 * 1024, 0, super_seeding, &p);

	int mask = alert::all_categories & ~(alert::progress_notification | alert::performance_warning | alert::stats_notification);
	ses1.set_alert_mask(mask);
	ses2.set_alert_mask(mask);
	ses3.set_alert_mask(mask);

	if (time_critical)
	{
		tor2.set_piece_deadline(2, 0);
		tor2.set_piece_deadline(5, 1000);
		tor2.set_piece_deadline(8, 2000);
	}

	float sum_dl_rate2 = 0.f;
	float sum_dl_rate3 = 0.f;
	int count_dl_rates2 = 0;
	int count_dl_rates3 = 0;

	for (int i = 0; i < 80; ++i)
	{
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");
		print_alerts(ses3, "ses3");

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();
		torrent_status st3 = tor3.status();

		if (st2.progress < 1.f && st2.progress > 0.5f)
		{
			sum_dl_rate2 += st2.download_payload_rate;
			++count_dl_rates2;
		}
		if (st3.progress < 1.f && st3.progress > 0.5f)
		{
			sum_dl_rate3 += st3.download_rate;
			++count_dl_rates3;
		}

		print_ses_rate(i, &st1, &st2, &st3);

		if (st2.is_seeding && st3.is_seeding) break;
		test_sleep(1000);
	}

	TEST_CHECK(tor2.status().is_seeding);
	TEST_CHECK(tor3.status().is_seeding);

	float average2 = sum_dl_rate2 / float(count_dl_rates2);
	float average3 = sum_dl_rate3 / float(count_dl_rates3);

	std::cerr << average2 << std::endl;
	std::cerr << "average rate: " << (average2 / 1000.f) << "kB/s - "
		<< (average3 / 1000.f) << "kB/s" << std::endl;

	if (tor2.status().is_seeding && tor3.status().is_seeding) std::cerr << "done\n";

	// make sure the files are deleted
	ses1.remove_torrent(tor1, session::delete_files);
	ses2.remove_torrent(tor2, session::delete_files);
	ses3.remove_torrent(tor3, session::delete_files);

	std::auto_ptr<alert> a = ses1.pop_alert();
	ptime end = time_now() + seconds(20);
	while (a.get() == 0 || dynamic_cast<torrent_deleted_alert*>(a.get()) == 0)
	{
		if (ses1.wait_for_alert(end - time_now()) == 0)
		{
			std::cerr << "wait_for_alert() expired" << std::endl;
			break;
		}
		a = ses1.pop_alert();
		assert(a.get());
		std::cerr << a->message() << std::endl;
	}

	TEST_CHECK(dynamic_cast<torrent_deleted_alert*>(a.get()) != 0);

	// there shouldn't be any alerts generated from now on
	// make sure that the timer in wait_for_alert() works
	// this should time out (ret == 0) and it should take
	// about 2 seconds
	ptime start = time_now_hires();
	alert const* ret;
	while ((ret = ses1.wait_for_alert(seconds(2))))
	{
		a = ses1.pop_alert();
		std::cerr << ret->message() << std::endl;
		start = time_now();
	}

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
	p3 = ses3.abort();

	TEST_CHECK(time_now_hires() - start < seconds(3));
	TEST_CHECK(time_now_hires() - start >= seconds(2));

	TEST_CHECK(!exists("tmp1_swarm/temporary"));
	TEST_CHECK(!exists("tmp2_swarm/temporary"));
	TEST_CHECK(!exists("tmp3_swarm/temporary"));

	remove_all("tmp1_swarm", ec);
	remove_all("tmp2_swarm", ec);
	remove_all("tmp3_swarm", ec);
}

int test_main()
{
	using namespace libtorrent;

	// with time critical pieces
	test_swarm(false, false, false, true);

	// with seed mode
	test_swarm(false, false, true);

	test_swarm();

	// with super seeding
	test_swarm(true);
	
	// with strict super seeding
	test_swarm(true, true);

	return 0;
}

