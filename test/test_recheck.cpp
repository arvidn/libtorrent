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
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"
#include <tuple>
#include <functional>

#include "test.hpp"
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
		std::printf("%f s -  %f %%\n"
			, total_milliseconds(clock_type::now() - last_change) / 1000.0
			, st.progress_ppm / 10000.0);
		if (st.progress_ppm == 1000000) return;
		if (st.progress_ppm != last_progress)
		{
			last_progress = st.progress_ppm;
			last_change = clock_type::now();
		}
		if (clock_type::now() - last_change > seconds(20)) break;
		std::this_thread::sleep_for(lt::seconds(1));
	}
	TEST_ERROR("torrent did not finish");
}

} // anonymous namespace

TORRENT_TEST(recheck)
{
	error_code ec;
	settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, "0.0.0.0:48675");
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
