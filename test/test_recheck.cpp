/*

Copyright (c) 2014-2022, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"
#include <tuple>
#include <functional>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"

#include <fstream>
#include <iostream>

using namespace lt;

namespace {

void wait_for_complete(lt::session& ses, torrent_handle h)
{
	int last_progress = 0;
	clock_type::time_point last_change = clock_type::now();
	for (int i = 0; i < 200; ++i)
	{
		print_alerts(ses, "ses1");
		torrent_status st = h.status();
		std::printf("%d ms -  %f %%\n"
			, int(total_milliseconds(clock_type::now() - last_change))
			, st.progress_ppm / 10000.0);
		if (st.progress_ppm == 1000000) return;
		if (st.progress_ppm != last_progress)
		{
			last_progress = st.progress_ppm;
			last_change = clock_type::now();
		}
		if (clock_type::now() - last_change > seconds(30)) break;
		std::this_thread::sleep_for(lt::seconds(1));
	}
	TEST_ERROR("torrent did not finish");
}

} // anonymous namespace

TORRENT_TEST(recheck)
{
	error_code ec;
	settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, test_listen_interface());
	sett.set_bool(settings_pack::enable_upnp, false);
	sett.set_bool(settings_pack::enable_natpmp, false);
	sett.set_bool(settings_pack::enable_lsd, false);
	sett.set_bool(settings_pack::enable_dht, false);
	lt::session ses1(sett);
	create_directory("tmp1_recheck", ec);
	if (ec) std::printf("create_directory: %s\n", ec.message().c_str());
	std::ofstream file("tmp1_recheck/temporary");
	std::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 4 * 1024 * 1024
		, 7, false);
	file.close();

	add_torrent_params param;
	param.flags &= ~torrent_flags::paused;
	param.flags &= ~torrent_flags::auto_managed;
	param.ti = t;
	param.save_path = "tmp1_recheck";
	param.flags |= torrent_flags::seed_mode;
	torrent_handle tor1 = ses1.add_torrent(param, ec);
	if (ec) std::printf("add_torrent: %s\n", ec.message().c_str());

	wait_for_listen(ses1, "ses1");

	tor1.force_recheck();

	torrent_status st1 = tor1.status();
	TEST_CHECK(st1.progress_ppm <= 1000000);
	wait_for_complete(ses1, tor1);

	tor1.force_recheck();

	st1 = tor1.status();
	TEST_CHECK(st1.progress_ppm <= 1000000);
	wait_for_complete(ses1, tor1);
}
