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

#include "test.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/torrent_status.hpp"
#include <boost/tuple/tuple.hpp>

#include "setup_transfer.hpp"
#include <iostream>

void test_pex()
{
	using namespace libtorrent;
	namespace lt = libtorrent;

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;
	session_proxy p3;

	int mask = alert::all_categories
		& ~(alert::progress_notification
			| alert::performance_warning
			| alert::stats_notification);

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	settings_pack pack;
	pack.set_int(settings_pack::alert_mask, mask);
	pack.set_int(settings_pack::download_rate_limit, 2000);
	pack.set_int(settings_pack::upload_rate_limit, 2000);
	pack.set_int(settings_pack::max_retry_port_bind, 800);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48200");

	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);

	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);

	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:49200");

	lt::session ses3(pack);

	// make the peer connecting the two worthless to transfer
	// data, to force peer 3 to connect directly to peer 1 through pex
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:50200");
	lt::session ses2(pack);

	ses1.add_extension(create_ut_pex_plugin);
	ses2.add_extension(create_ut_pex_plugin);

	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;

	boost::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true, false, false, "_pex");

	ses2.apply_settings(pack);

	test_sleep(100);

	// in this test, ses1 is a seed, ses2 is connected to ses1 and ses3.
	// the expected behavior is that ses2 will introduce ses1 and ses3 to each other
	error_code ec;
	tor2.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec), ses1.listen_port()));
	tor2.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec), ses3.listen_port()));

	torrent_status st1;
	torrent_status st2;
	torrent_status st3;
	for (int i = 0; i < 610; ++i)
	{
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");
		print_alerts(ses3, "ses3");

		st1 = tor1.status();
		st2 = tor2.status();
		st3 = tor3.status();

		print_ses_rate(i / 10.f, &st1, &st2, &st3);

		// this is the success condition
		if (st1.num_peers == 2 && st2.num_peers == 2 && st3.num_peers == 2)
			break;

		// this suggests that we failed. If session 3 completes without
		// actually connecting to session 1, everything was transferred
		// through session 2
		if (st3.state == torrent_status::seeding) break;

		test_sleep(100);
	}

	TEST_CHECK(st1.num_peers == 2 && st2.num_peers == 2 && st3.num_peers == 2)

	if (!tor2.status().is_seeding && tor3.status().is_seeding) std::cerr << "done\n";

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
	p3 = ses3.abort();
}
#endif // TORRENT_DISABLE_EXTENSIONS

TORRENT_TEST(pex)
{
#ifndef TORRENT_DISABLE_EXTENSIONS
	using namespace libtorrent;

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_pex", ec);
	remove_all("tmp2_pex", ec);
	remove_all("tmp3_pex", ec);

	test_pex();

	remove_all("tmp1_pex", ec);
	remove_all("tmp2_pex", ec);
	remove_all("tmp3_pex", ec);
#endif // TORRENT_DISABLE_EXTENSIONS
}


