/*

Copyright (c) 2010, 2012-2021, Arvid Norberg
Copyright (c) 2016-2018, Alden Torres
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
#include "libtorrent/bencode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/utp_stream.hpp"
#include <tuple>
#include <functional>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "test_utils.hpp"
#include <fstream>

#ifdef TORRENT_UTP_LOG_ENABLE
#include "libtorrent/utp_stream.hpp"
#endif

using namespace lt;

namespace {

void test_transfer()
{
#ifdef TORRENT_UTP_LOG_ENABLE
	lt::set_utp_stream_logging(true);
#endif

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_utp", ec);
	remove_all("tmp2_utp", ec);

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	settings_pack pack = settings();
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_bool(settings_pack::enable_outgoing_tcp, false);
	pack.set_bool(settings_pack::enable_incoming_tcp, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::prefer_udp_trackers, false);
	pack.set_int(settings_pack::min_reconnect_time, 1);
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses2(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	ec.clear();
	create_directory("tmp1_utp", ec);
	if (ec)
	{
		std::printf("ERROR: failed to create test directory \"tmp1_utp\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
	}
	std::ofstream file("tmp1_utp/temporary");
	std::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 128 * 1024, 6, false);
	file.close();

	// for performance testing
	add_torrent_params atp;
	atp.flags &= ~torrent_flags::paused;
	atp.flags &= ~torrent_flags::auto_managed;
//	atp.storage = &disabled_storage_constructor;

	// test using piece sizes smaller than 16kB
	std::tie(tor1, tor2, std::ignore) = setup_transfer(&ses1, &ses2, nullptr
		, true, false, true, "_utp", 0, &t, false, &atp);

	const int timeout = 16;

	for (int i = 0; i < timeout; ++i)
	{
		print_alerts(ses1, "ses1", true, true);
		print_alerts(ses2, "ses2", true, true);

		std::this_thread::sleep_for(lt::milliseconds(500));

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

} // anonymous namespace

TORRENT_TEST(utp)
{
	test_transfer();

	error_code ec;
	remove_all("tmp1_utp", ec);
	remove_all("tmp2_utp", ec);
}

TORRENT_TEST(compare_less_wrap)
{
	using lt::aux::compare_less_wrap;

	TEST_CHECK(compare_less_wrap(1, 2, 0xffff));
	TEST_CHECK(!compare_less_wrap(2, 1, 0xffff));
	TEST_CHECK(compare_less_wrap(100, 200, 0xffff));
	TEST_CHECK(!compare_less_wrap(200, 100, 0xffff));
	TEST_CHECK(compare_less_wrap(0xfff0, 0x000f, 0xffff)); // wrap
	TEST_CHECK(!compare_less_wrap(0xfff0, 0xff00, 0xffff));
}
