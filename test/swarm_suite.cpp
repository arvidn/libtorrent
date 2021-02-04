/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016, Eugene Shalygin
Copyright (c) 2017, Steven Siloti
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
#include "libtorrent/alert_types.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/aux_/path.hpp"
#include <iostream>
#include <tuple>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "swarm_suite.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// warning C4706: assignment within conditional expression
#pragma warning( disable : 4706 )
#endif

void test_swarm(test_flags_t const flags)
{
	using namespace lt;

	std::printf("\n\n ==== TEST SWARM === %s%s%s%s%s%s%s ===\n\n\n"
		, (flags & test_flags::super_seeding) ? "super-seeding ": ""
		, (flags & test_flags::strict_super_seeding) ? "strict-super-seeding ": ""
		, (flags & test_flags::seed_mode) ? "seed-mode ": ""
		, (flags & test_flags::time_critical) ? "time-critical ": ""
		, (flags & test_flags::suggest) ? "suggest ": ""
		, (flags & test_flags::v1_meta) ? "v1-meta ": ""
		, (flags & test_flags::v2_meta) ? "v2-meta ": ""
		);

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

	settings_pack pack = settings();
	pack.set_bool(settings_pack::allow_multiple_connections_per_ip, true);

#if TORRENT_ABI_VERSION == 1
	if (flags & test_flags::strict_super_seeding)
		pack.set_bool(settings_pack::strict_super_seeding, true);
#endif

	if (flags & test_flags::suggest)
		pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);

	// this is to avoid everything finish from a single peer
	// immediately. To make the swarm actually connect all
	// three peers before finishing.
	float const rate_limit = 100000;

	int const port = static_cast<int>(lt::random(100));
	char iface[50];
	std::snprintf(iface, sizeof(iface), "0.0.0.0:480%02d", port);
	pack.set_int(settings_pack::upload_rate_limit, int(rate_limit));
	pack.set_str(settings_pack::listen_interfaces, iface);
	pack.set_int(settings_pack::max_retry_port_bind, 1000);

	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);

	lt::session ses1(pack);

	std::snprintf(iface, sizeof(iface), "0.0.0.0:490%02d", port);
	pack.set_str(settings_pack::listen_interfaces, iface);
	pack.set_int(settings_pack::download_rate_limit, int(rate_limit / 2));
	pack.set_int(settings_pack::upload_rate_limit, int(rate_limit));
	lt::session ses2(pack);

	std::snprintf(iface, sizeof(iface), "0.0.0.0:500%02d", port);
	pack.set_str(settings_pack::listen_interfaces, iface);
	lt::session ses3(pack);

	torrent_handle tor1;
	torrent_handle tor2;
	torrent_handle tor3;

	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	if (flags & test_flags::seed_mode) p.flags |= torrent_flags::seed_mode;
	// test v1 metadata using piece sizes smaller than 16kB
	int const piece_size = (flags & test_flags::v1_meta) ? 8 * 1024 : 16 * 1024;
	std::tie(tor1, tor2, tor3) = setup_transfer(&ses1, &ses2, &ses3, true
		, false, true, "_swarm", piece_size, nullptr, bool(flags & test_flags::super_seeding), &p
		, true, false, nullptr
		, (flags & test_flags::v1_meta) ? create_torrent::v1_only
		: (flags & test_flags::v2_meta) ? create_torrent::v2_only
		: create_flags_t{});

	if (flags & test_flags::time_critical)
	{
		tor2.set_piece_deadline(piece_index_t(2), 0);
		tor2.set_piece_deadline(piece_index_t(5), 1000);
		tor2.set_piece_deadline(piece_index_t(8), 2000);
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

		if (flags & test_flags::super_seeding)
		{
			TEST_CHECK(st1.is_seeding);
			TEST_CHECK(tor1.flags() & torrent_flags::super_seeding);
		}

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

		print_ses_rate(float(i), &st1, &st2, &st3);

		if (st2.is_seeding && st3.is_seeding) break;
		std::this_thread::sleep_for(lt::milliseconds(1000));
	}

	TEST_CHECK(tor2.status().is_seeding);
	TEST_CHECK(tor3.status().is_seeding);

	float average2 = count_dl_rates2 > 0 ? sum_dl_rate2 / float(count_dl_rates2) : -1;
	float average3 = count_dl_rates3 > 0 ? sum_dl_rate3 / float(count_dl_rates3) : -1;

	std::cout << "average rate: " << (average2 / 1000.f) << "kB/s - "
		<< (average3 / 1000.f) << "kB/s" << std::endl;

	if (tor2.status().is_seeding && tor3.status().is_seeding) std::cout << "done\n";

	// make sure the files are deleted
	ses1.remove_torrent(tor1, lt::session::delete_files);
	ses2.remove_torrent(tor2, lt::session::delete_files);
	ses3.remove_torrent(tor3, lt::session::delete_files);

	alert const* a = wait_for_alert(ses1, torrent_deleted_alert::alert_type, "swarm_suite");

	TEST_CHECK(alert_cast<torrent_deleted_alert>(a) != nullptr);

	// there shouldn't be any alerts generated from now on
	// make sure that the timer in wait_for_alert() works
	// this should time out (ret == 0) and it should take
	// about 2 seconds
	time_point start = clock_type::now();
	alert const* ret;
	while ((ret = ses1.wait_for_alert(seconds(2))))
	{
		std::printf("wait returned: %d ms\n"
			, int(total_milliseconds(clock_type::now() - start)));
		std::vector<alert*> alerts;
		ses1.pop_alerts(&alerts);
		for (auto ap : alerts)
		{
			std::printf("%s\n", ap->message().c_str());
		}
		start = clock_type::now();
	}

	std::printf("loop returned: %d ms\n"
		, int(total_milliseconds(clock_type::now() - start)));

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();
	p3 = ses3.abort();

	time_point const end = clock_type::now();

	std::printf("time: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(3000));
	TEST_CHECK(end - start > milliseconds(1900));

	TEST_CHECK(!exists("tmp1_swarm/temporary"));
	TEST_CHECK(!exists("tmp2_swarm/temporary"));
	TEST_CHECK(!exists("tmp3_swarm/temporary"));

	remove_all("tmp1_swarm", ec);
	remove_all("tmp2_swarm", ec);
	remove_all("tmp3_swarm", ec);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
