/*

Copyright (c) 2013, Arvid Norberg
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
#include <boost/bind.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

TORRENT_TEST(session)
{
	settings_pack p;
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	settings_pack sett;
	sett.set_int(settings_pack::cache_size, 100);
	sett.set_int(settings_pack::max_queued_disk_bytes, 1000 * 16 * 1024);

	ses.apply_settings(sett);

	// verify that we get the appropriate performance warning because
	// we're allowing a larger queue than we have cache.


	alert const* a;
	for (;;)
	{
		a = wait_for_alert(ses, performance_alert::alert_type, "ses1");

		if (a == NULL) break;
		TEST_EQUAL(a->type(), performance_alert::alert_type);

		if (alert_cast<performance_alert>(a)->warning_code
			== performance_alert::too_high_disk_queue_limit)
			break;
	}

	TEST_CHECK(a);

	sett.set_int(settings_pack::unchoke_slots_limit, 0);
	ses.apply_settings(sett);
	TEST_CHECK(ses.get_settings().get_int(settings_pack::unchoke_slots_limit) == 0);

	sett.set_int(settings_pack::unchoke_slots_limit, -1);
	ses.apply_settings(sett);
	TEST_CHECK(ses.get_settings().get_int(settings_pack::unchoke_slots_limit) == -1);

	sett.set_int(settings_pack::unchoke_slots_limit, 8);
	ses.apply_settings(sett);
	TEST_CHECK(ses.get_settings().get_int(settings_pack::unchoke_slots_limit) == 8);

	// make sure the destructor waits properly
	// for the asynchronous call to set the alert
	// mask completes, before it goes on to destruct
	// the session object
}

TORRENT_TEST(session_stats)
{
	std::vector<stats_metric> stats = session_stats_metrics();
	std::sort(stats.begin(), stats.end()
		, boost::bind(&stats_metric::value_index, _1)
		< boost::bind(&stats_metric::value_index, _2));

	TEST_EQUAL(stats.size(), lt::counters::num_counters);
	// make sure every stat index is represented in the stats_metric vector
	for (int i = 0; i < int(stats.size()); ++i)
	{
		TEST_EQUAL(stats[i].value_index, i);
	}
}
#if __cplusplus >= 201103L

template <typename Set, typename Save, typename Default, typename Load>
void test_save_restore(Set setup, Save s, Default d, Load l)
{
	entry st;
	{
		settings_pack p;
		setup(p);
		lt::session ses(p);
		s(ses, st);
	}

	{
		settings_pack p;
		d(p);
		lt::session ses(p);
		// the loading function takes a bdecode_node, so we have to transform the
		// entry
		printf("%s\n", st.to_string().c_str());
		std::vector<char> buf;
		bencode(std::back_inserter(buf), st);
		bdecode_node state;
		error_code ec;
		int ret = bdecode(buf.data(), buf.data() + buf.size()
			, state, ec, nullptr, 100, 1000);
		TEST_EQUAL(ret, 0);
		if (ec)
		{
			printf("bdecode: %s\n", ec.message().c_str());
			printf("%s\n", std::string(buf.data(), buf.size()).c_str());
		}
		TEST_CHECK(!ec);
		l(ses, state);
	}
}

TORRENT_TEST(save_restore_state)
{
	test_save_restore(
		[](settings_pack& p) {
			// set the cache size
			p.set_int(settings_pack::cache_size, 1337);
		},
		[](lt::session& ses, entry& st) {
			ses.save_state(st);
		},
		[](settings_pack& p) {
			p.set_int(settings_pack::cache_size, 90);
		},
		[](lt::session& ses, bdecode_node& st) {
			ses.load_state(st);
			// make sure we loaded the cache size correctly
			settings_pack sett = ses.get_settings();
			TEST_EQUAL(sett.get_int(settings_pack::cache_size), 1337);
		});
}

TORRENT_TEST(save_restore_state_save_filter)
{
	test_save_restore(
		[](settings_pack& p) {
			// set the cache size
			p.set_int(settings_pack::cache_size, 1337);
		},
		[](lt::session& ses, entry& st) {
			// save everything _but_ the settings
			ses.save_state(st, ~session::save_settings);
		},
		[](settings_pack& p) {
			p.set_int(settings_pack::cache_size, 90);
		},
		[](lt::session& ses, bdecode_node& st) {
			ses.load_state(st);
			// make sure whatever we loaded did not include the cache size
			settings_pack sett = ses.get_settings();
			TEST_EQUAL(sett.get_int(settings_pack::cache_size), 90);
		});
}

TORRENT_TEST(save_restore_state_load_filter)
{
	test_save_restore(
		[](settings_pack& p) {
			// set the cache size
			p.set_int(settings_pack::cache_size, 1337);
		},
		[](lt::session& ses, entry& st) {
			// save everything
			ses.save_state(st);
		},
		[](settings_pack& p) {
			p.set_int(settings_pack::cache_size, 90);
		},
		[](lt::session& ses, bdecode_node& st) {
			// load everything _but_ the settings
			ses.load_state(st, ~session::save_settings);
			settings_pack sett = ses.get_settings();
			TEST_EQUAL(sett.get_int(settings_pack::cache_size), 90);
		});
}

#endif

