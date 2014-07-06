/*

Copyright (c) 2012, Arvid Norberg
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
#include "libtorrent/error_code.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

#include <fstream>
#include <iostream>

using namespace libtorrent;
namespace lt = libtorrent;

const int mask = alert::all_categories & ~(alert::performance_warning | alert::stats_notification);

void wait_for_complete(lt::session& ses, torrent_handle h)
{
	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses, "ses1");
		torrent_status st = h.status();
		fprintf(stderr, "%f %%\n", st.progress_ppm / 10000.f);
		if (st.progress_ppm == 1000000) return;
		test_sleep(500);
	}
	TEST_CHECK(false);
}

int test_main()
{
	error_code ec;
	lt::session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48675, 49000), "0.0.0.0", 0, mask);
	create_directory("tmp1_recheck", ec);
	if (ec) fprintf(stderr, "create_directory: %s\n", ec.message().c_str());
	std::ofstream file("tmp1_recheck/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, 4 * 1024 * 1024, 7);
	file.close();

	add_torrent_params param;
	param.flags &= ~add_torrent_params::flag_paused;
	param.flags &= ~add_torrent_params::flag_auto_managed;
	param.ti = t;
	param.save_path = "tmp1_recheck";
	param.flags |= add_torrent_params::flag_seed_mode;
	torrent_handle tor1 = ses1.add_torrent(param, ec);
	if (ec) fprintf(stderr, "add_torrent: %s\n", ec.message().c_str());

	wait_for_listen(ses1, "ses1");

	tor1.force_recheck();

	torrent_status st1 = tor1.status();
	TEST_CHECK(st1.progress_ppm <= 1000000);
	wait_for_complete(ses1, tor1);

	tor1.force_recheck();

	st1 = tor1.status();
	TEST_CHECK(st1.progress_ppm <= 1000000);
	wait_for_complete(ses1, tor1);

	return 0;
}
