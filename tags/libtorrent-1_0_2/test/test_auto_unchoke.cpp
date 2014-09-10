/*

Copyright (c) 2011, Arvid Norberg
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
#include <boost/tuple/tuple.hpp>
#include <iostream>

#include "test.hpp"
#include "setup_transfer.hpp"

void test_swarm()
{
	using namespace libtorrent;

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;
	session_proxy p3;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48010, 49000), "0.0.0.0", 0);
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49010, 50000), "0.0.0.0", 0);
	session ses3(fingerprint("LT", 0, 1, 0, 0), std::make_pair(50010, 51000), "0.0.0.0", 0);

	ses1.set_alert_mask(alert::all_categories);
	ses2.set_alert_mask(alert::all_categories);
	ses3.set_alert_mask(alert::all_categories);

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	float rate_limit = 100000;

	session_settings settings;
	settings.allow_multiple_connections_per_ip = true;
	settings.ignore_limits_on_local_network = false;
	settings.choking_algorithm = session_settings::auto_expand_choker;
	settings.upload_rate_limit = rate_limit;
	settings.unchoke_slots_limit = 1;
	ses1.set_settings(settings);

	settings.upload_rate_limit = rate_limit / 10;
	settings.download_rate_limit = rate_limit / 5;
	settings.unchoke_slots_limit = 0;
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

	boost::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true, false, true, "_unchoke");	

	session_status st = ses1.status();
	TEST_EQUAL(st.allowed_upload_slots, 1);
	for (int i = 0; i < 100; ++i)
	{
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");
		print_alerts(ses3, "ses3");

		st = ses1.status();
		fprintf(stderr, "allowed unchoked: %d\n", st.allowed_upload_slots);
		if (st.allowed_upload_slots >= 2) break;

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();
		torrent_status st3 = tor3.status();

		print_ses_rate(i, &st1, &st2, &st3);

		test_sleep(100);
	}

	TEST_CHECK(st.allowed_upload_slots >= 2);

	// make sure the files are deleted
	ses1.remove_torrent(tor1, session::delete_files);
	ses2.remove_torrent(tor2, session::delete_files);
	ses3.remove_torrent(tor3, session::delete_files);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
	p3 = ses3.abort();
}

int test_main()
{
	using namespace libtorrent;

	// in case the previous run was t r catch (std::exception&) {}erminated
	error_code ec;
	remove_all("./tmp1_unchoke", ec);
	remove_all("./tmp2_unchoke", ec);
	remove_all("./tmp3_unchoke", ec);

	test_swarm();
	
	test_sleep(2000);
	TEST_CHECK(!exists("./tmp1_unchoke/temporary"));
	TEST_CHECK(!exists("./tmp2_unchoke/temporary"));
	TEST_CHECK(!exists("./tmp3_unchoke/temporary"));

	remove_all("./tmp1_unchoke", ec);
	remove_all("./tmp2_unchoke", ec);
	remove_all("./tmp3_unchoke", ec);

	return 0;
}

