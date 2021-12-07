/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2018, d-komarov
Copyright (c) 2007-2010, 2013-2021, Arvid Norberg
Copyright (c) 2016, Luca Bruno
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/aux_/path.hpp"
#include <tuple>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include <iostream>

namespace {

void test_lsd()
{
	using namespace lt;

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	settings_pack pack;
	pack.set_int(settings_pack::alert_mask, alert_category::error
		| alert_category::session_log
		| alert_category::torrent_log
		| alert_category::peer_log
		| alert_category::ip_block
		| alert_category::status);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_bool(settings_pack::enable_lsd, true);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
#if TORRENT_ABI_VERSION == 1
	pack.set_bool(settings_pack::rate_limit_utp, true);
#endif

	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses2(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	using std::ignore;
	std::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, nullptr, true, false, false, "_lsd"
		, 16 * 1024, nullptr, false, nullptr, false);

	auto const start_time = lt::clock_type::now();
	for (int i = 0; i < 30; ++i)
	{
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		print_ses_rate(start_time, &st1, &st2);

		if (st2.is_seeding /*&& st3.is_seeding*/) break;
		std::this_thread::sleep_for(lt::milliseconds(1000));
	}

	TEST_CHECK(tor2.status().is_seeding);

	if (tor2.status().is_seeding) std::cout << "done\n";

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
}

} // anonymous namespace

TORRENT_TEST(lsd)
{
	using namespace lt;

	// in case the previous run was terminated
	error_code ec;
	remove_all("./tmp1_lsd", ec);
	remove_all("./tmp2_lsd", ec);
	remove_all("./tmp3_lsd", ec);

	test_lsd();

	remove_all("./tmp1_lsd", ec);
	remove_all("./tmp2_lsd", ec);
	remove_all("./tmp3_lsd", ec);
}



