/*

Copyright (c) 2008-2009, 2012-2019, 2021, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2018, Alden Torres
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
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/path.hpp"
#include <tuple>
#include <iostream>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"

namespace {

void test_swarm()
{
	using namespace lt;

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;
	session_proxy p3;

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	float rate_limit = 50000;

	settings_pack pack = settings();
	// run the choker once per second, to make it more likely to actually trigger
	// during the test.
	pack.set_int(settings_pack::unchoke_interval, 1);

	pack.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
	pack.set_int(settings_pack::choking_algorithm, settings_pack::rate_based_choker);
	pack.set_int(settings_pack::upload_rate_limit, int(rate_limit));
	pack.set_int(settings_pack::unchoke_slots_limit, 1);
	pack.set_int(settings_pack::max_retry_port_bind, 900);
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_dht, false);
#if TORRENT_ABI_VERSION == 1
	pack.set_bool(settings_pack::rate_limit_utp, true);
#endif

	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);

	lt::session ses1(pack);

	pack.set_int(settings_pack::upload_rate_limit, int(rate_limit / 10));
	pack.set_int(settings_pack::download_rate_limit, int(rate_limit / 5));
	pack.set_int(settings_pack::unchoke_slots_limit, 0);
	pack.set_int(settings_pack::choking_algorithm, settings_pack::fixed_slots_choker);
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());

	lt::session ses2(pack);

	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());

	lt::session ses3(pack);

	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;

	std::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true, false, true, "_unchoke");

	std::map<std::string, std::int64_t> cnt = get_counters(ses1);

	std::printf("allowed_upload_slots: %d\n", int(cnt["ses.num_unchoke_slots"]));
	TEST_EQUAL(cnt["ses.num_unchoke_slots"], 1);
	for (int i = 0; i < 200; ++i)
	{
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");
		print_alerts(ses3, "ses3");

		cnt = get_counters(ses1);
		std::printf("allowed unchoked: %d\n", int(cnt["ses.num_unchoke_slots"]));
		if (cnt["ses.num_unchoke_slots"] >= 2) break;

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();
		torrent_status st3 = tor3.status();

		print_ses_rate(i / 10.f, &st1, &st2, &st3);

		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	TEST_CHECK(cnt["ses.num_unchoke_slots"] >= 2);

	// make sure the files are deleted
	ses1.remove_torrent(tor1, lt::session::delete_files);
	ses2.remove_torrent(tor2, lt::session::delete_files);
	ses3.remove_torrent(tor3, lt::session::delete_files);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
	p3 = ses3.abort();
}

} // anonymous namespace

TORRENT_TEST(auto_unchoke)
{
	using namespace lt;

	// in case the previous run was terminated
	error_code ec;
	remove_all("./tmp1_unchoke", ec);
	remove_all("./tmp2_unchoke", ec);
	remove_all("./tmp3_unchoke", ec);

	test_swarm();

	TEST_CHECK(!exists("./tmp1_unchoke/temporary"));
	TEST_CHECK(!exists("./tmp2_unchoke/temporary"));
	TEST_CHECK(!exists("./tmp3_unchoke/temporary"));

	remove_all("./tmp1_unchoke", ec);
	remove_all("./tmp2_unchoke", ec);
	remove_all("./tmp3_unchoke", ec);
}
