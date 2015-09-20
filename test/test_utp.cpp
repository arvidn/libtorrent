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
namespace lt = libtorrent;
using boost::tuples::ignore;

void test_transfer()
{
	// in case the previous run was terminated
	error_code ec;
	remove_all("./tmp1_utp", ec);
	remove_all("./tmp2_utp", ec);

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	const int mask = alert::all_categories
		& ~(alert::progress_notification
			| alert::performance_warning
			| alert::stats_notification);

	settings_pack pack;
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_int(settings_pack::alert_mask, mask);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_bool(settings_pack::enable_outgoing_tcp, false);
	pack.set_bool(settings_pack::enable_incoming_tcp, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::prefer_udp_trackers, false);
	pack.set_int(settings_pack::min_reconnect_time, 1);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48885");
	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:49885");
	lt::session ses2(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("./tmp1_utp", ec);
	std::ofstream file("./tmp1_utp/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 128 * 1024, 6, false);
	file.close();

	// for performance testing
	add_torrent_params atp;
	atp.flags &= ~add_torrent_params::flag_paused;
	atp.flags &= ~add_torrent_params::flag_auto_managed;
//	atp.storage = &disabled_storage_constructor;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_utp", 0, &t, false, &atp);

#ifdef TORRENT_USE_VALGRIND
	const int timeout = 16;
#else
	const int timeout = 8;
#endif

	for (int i = 0; i < timeout; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true);
		print_alerts(ses2, "ses2", true, true, true);

		test_sleep(500);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		print_ses_rate(i / 2.f, &st1, &st2);

		if (st2.is_finished) break;

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading);
	}

	TEST_CHECK(tor1.status().is_finished);
	TEST_CHECK(tor2.status().is_finished);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
}

TORRENT_TEST(utp)
{
	using namespace libtorrent;

	test_transfer();

	error_code ec;
	remove_all("./tmp1_utp", ec);
	remove_all("./tmp2_utp", ec);
}

