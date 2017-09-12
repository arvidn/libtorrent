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
#include <functional>

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/session_stats.hpp"
#include "settings.hpp"

#include <fstream>

using namespace std::placeholders;
using namespace lt;

TORRENT_TEST(session)
{
	settings_pack p = settings();
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	settings_pack sett = settings();
	sett.set_int(settings_pack::num_optimistic_unchoke_slots, 10);
	sett.set_int(settings_pack::unchoke_slots_limit, 10);
	sett.set_int(settings_pack::resolver_cache_timeout, 1000);

	ses.apply_settings(sett);

	// verify that we get the appropriate performance warning

	alert const* a;
	for (;;)
	{
		a = wait_for_alert(ses, performance_alert::alert_type, "ses1");

		if (a == nullptr) break;
		TEST_EQUAL(a->type(), performance_alert::alert_type);

		if (alert_cast<performance_alert>(a)->warning_code
			== performance_alert::too_many_optimistic_unchoke_slots)
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

	TEST_EQUAL(ses.get_settings().get_int(settings_pack::resolver_cache_timeout), 1000);
	sett.set_int(settings_pack::resolver_cache_timeout, 1001);
	ses.apply_settings(sett);
	TEST_EQUAL(ses.get_settings().get_int(settings_pack::resolver_cache_timeout), 1001);

	// make sure the destructor waits properly
	// for the asynchronous call to set the alert
	// mask completes, before it goes on to destruct
	// the session object
}

TORRENT_TEST(async_add_torrent_duplicate_error)
{
	settings_pack p = settings();
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	add_torrent_params atp;
	atp.info_hash.assign("abababababababababab");
	atp.save_path = ".";
	ses.async_add_torrent(atp);

	auto* a = alert_cast<add_torrent_alert>(wait_for_alert(ses, add_torrent_alert::alert_type, "ses"));
	TEST_CHECK(a);
	if (a == nullptr) return;

	atp.flags |= torrent_flags::duplicate_is_error;
	ses.async_add_torrent(atp);
	a = alert_cast<add_torrent_alert>(wait_for_alert(ses, add_torrent_alert::alert_type, "ses"));
	TEST_CHECK(a);
	if (a == nullptr) return;
	TEST_CHECK(!a->handle.is_valid());
	TEST_CHECK(a->error);
}

TORRENT_TEST(async_add_torrent_duplicate)
{
	settings_pack p = settings();
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	add_torrent_params atp;
	atp.info_hash.assign("abababababababababab");
	atp.save_path = ".";
	ses.async_add_torrent(atp);

	auto* a = alert_cast<add_torrent_alert>(wait_for_alert(ses, add_torrent_alert::alert_type, "ses"));
	TEST_CHECK(a);
	if (a == nullptr) return;
	torrent_handle h = a->handle;
	TEST_CHECK(!a->error);

	atp.flags &= ~torrent_flags::duplicate_is_error;
	ses.async_add_torrent(atp);
	a = alert_cast<add_torrent_alert>(wait_for_alert(ses, add_torrent_alert::alert_type, "ses"));
	TEST_CHECK(a);
	if (a == nullptr) return;
	TEST_CHECK(a->handle == h);
	TEST_CHECK(!a->error);
}

TORRENT_TEST(async_add_torrent_duplicate_back_to_back)
{
	settings_pack p = settings();
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	add_torrent_params atp;
	atp.info_hash.assign("abababababababababab");
	atp.save_path = ".";
	atp.flags |= torrent_flags::paused;
	atp.flags &= ~torrent_flags::apply_ip_filter;
	atp.flags &= ~torrent_flags::auto_managed;
	ses.async_add_torrent(atp);

	atp.flags &= ~torrent_flags::duplicate_is_error;
	ses.async_add_torrent(atp);

	auto* a = alert_cast<add_torrent_alert>(wait_for_alert(ses
			, add_torrent_alert::alert_type, "ses", pop_alerts::cache_alerts));
	TEST_CHECK(a);
	if (a == nullptr) return;
	torrent_handle h = a->handle;
	TEST_CHECK(!a->error);

	a = alert_cast<add_torrent_alert>(wait_for_alert(ses
		, add_torrent_alert::alert_type, "ses", pop_alerts::cache_alerts));
	TEST_CHECK(a);
	if (a == nullptr) return;
	TEST_CHECK(a->handle == h);
	TEST_CHECK(!a->error);

	torrent_status st = h.status();
	TEST_CHECK(st.flags & torrent_flags::paused);
	TEST_CHECK(!(st.flags & torrent_flags::apply_ip_filter));
	TEST_CHECK(!(st.flags & torrent_flags::auto_managed));
}

TORRENT_TEST(load_empty_file)
{
	settings_pack p = settings();
	p.set_int(settings_pack::alert_mask, ~0);
	lt::session ses(p);

	add_torrent_params atp;
	error_code ignore_errors;
	atp.ti = std::make_shared<torrent_info>("", std::ref(ignore_errors), from_span);
	atp.save_path = ".";
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(atp), ec);

	TEST_CHECK(!h.is_valid());
	TEST_CHECK(ec == error_code(errors::no_metadata))
}

TORRENT_TEST(session_stats)
{
	std::vector<stats_metric> stats = session_stats_metrics();
	std::sort(stats.begin(), stats.end()
		, [](stats_metric const& lhs, stats_metric const& rhs)
		{ return lhs.value_index < rhs.value_index; });

	TEST_EQUAL(stats.size(), lt::counters::num_counters);
	// make sure every stat index is represented in the stats_metric vector
	for (int i = 0; i < int(stats.size()); ++i)
	{
		TEST_EQUAL(stats[i].value_index, i);
	}

	TEST_EQUAL(lt::find_metric_idx("peer.incoming_connections")
		, lt::counters::incoming_connections);
}

TORRENT_TEST(paused_session)
{
	lt::session s(settings());
	s.pause();

	lt::add_torrent_params ps;
	std::ofstream file("temporary");
	ps.ti = ::create_torrent(&file, "temporary", 16 * 1024, 13, false);
	ps.flags = lt::torrent_flags::paused;
	ps.save_path = ".";

	torrent_handle h = s.add_torrent(std::move(ps));

	std::this_thread::sleep_for(lt::milliseconds(2000));
	h.resume();
	std::this_thread::sleep_for(lt::milliseconds(1000));

	TEST_CHECK(!(h.flags() & torrent_flags::paused));
}

template <typename Set, typename Save, typename Default, typename Load>
void test_save_restore(Set setup, Save s, Default d, Load l)
{
	entry st;
	{
		settings_pack p = settings();
		setup(p);
		lt::session ses(p);
		s(ses, st);
	}

	{
		settings_pack p = settings();
		d(p);
		lt::session ses(p);
		// the loading function takes a bdecode_node, so we have to transform the
		// entry
		std::printf("%s\n", st.to_string().c_str());
		std::vector<char> buf;
		bencode(std::back_inserter(buf), st);
		bdecode_node state;
		error_code ec;
		int ret = bdecode(buf.data(), buf.data() + buf.size()
			, state, ec, nullptr, 100, 1000);
		TEST_EQUAL(ret, 0);
		if (ec)
		{
			std::printf("bdecode: %s\n", ec.message().c_str());
			std::printf("%s\n", std::string(buf.data(), buf.size()).c_str());
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
			p.set_int(settings_pack::request_queue_time, 1337);
		},
		[](lt::session& ses, entry& st) {
			ses.save_state(st);
		},
		[](settings_pack& p) {
			p.set_int(settings_pack::request_queue_time, 90);
		},
		[](lt::session& ses, bdecode_node& st) {
			ses.load_state(st);
			// make sure we loaded the cache size correctly
			settings_pack sett = ses.get_settings();
			TEST_EQUAL(sett.get_int(settings_pack::request_queue_time), 1337);
		});
}

TORRENT_TEST(save_restore_state_save_filter)
{
	test_save_restore(
		[](settings_pack& p) {
			// set the cache size
			p.set_int(settings_pack::request_queue_time, 1337);
		},
		[](lt::session& ses, entry& st) {
			// save everything _but_ the settings
			ses.save_state(st, ~session::save_settings);
		},
		[](settings_pack& p) {
			p.set_int(settings_pack::request_queue_time, 90);
		},
		[](lt::session& ses, bdecode_node& st) {
			ses.load_state(st);
			// make sure whatever we loaded did not include the cache size
			settings_pack sett = ses.get_settings();
			TEST_EQUAL(sett.get_int(settings_pack::request_queue_time), 90);
		});
}

TORRENT_TEST(save_restore_state_load_filter)
{
	test_save_restore(
		[](settings_pack& p) {
			// set the cache size
			p.set_int(settings_pack::request_queue_time, 1337);
		},
		[](lt::session& ses, entry& st) {
			// save everything
			ses.save_state(st);
		},
		[](settings_pack& p) {
			p.set_int(settings_pack::request_queue_time, 90);
		},
		[](lt::session& ses, bdecode_node& st) {
			// load everything _but_ the settings
			ses.load_state(st, ~session::save_settings);
			settings_pack sett = ses.get_settings();
			TEST_EQUAL(sett.get_int(settings_pack::request_queue_time), 90);
		});
}

TORRENT_TEST(session_shutdown)
{
	lt::settings_pack pack;
	lt::session ses(pack);
}

// make sure we don't restore peer_id from session state
TORRENT_TEST(save_state_peer_id)
{
	lt::settings_pack pack;
	pack.set_str(settings_pack::peer_fingerprint, "AAA");
	lt::session ses(pack);
	lt::peer_id const pid1 = ses.id();
	TEST_CHECK(pid1[0] == 'A');
	TEST_CHECK(pid1[1] == 'A');
	TEST_CHECK(pid1[2] == 'A');

	lt::entry st;
	ses.save_state(st);

	pack.set_str(settings_pack::peer_fingerprint, "foobar");
	ses.apply_settings(pack);

	lt::peer_id const pid2 = ses.id();
	TEST_CHECK(pid2[0] == 'f');
	TEST_CHECK(pid2[1] == 'o');
	TEST_CHECK(pid2[2] == 'o');
	TEST_CHECK(pid2[3] == 'b');
	TEST_CHECK(pid2[4] == 'a');
	TEST_CHECK(pid2[5] == 'r');


	std::vector<char> buf;
	bencode(std::back_inserter(buf), st);
	bdecode_node state;
	error_code ec;
	int ret = bdecode(buf.data(), buf.data() + buf.size()
		, state, ec, nullptr, 100, 1000);
	TEST_EQUAL(ret, 0);
	ses.load_state(state);

	lt::peer_id const pid3 = ses.id();
	TEST_CHECK(pid3[0] == 'f');
	TEST_CHECK(pid3[1] == 'o');
	TEST_CHECK(pid3[2] == 'o');
	TEST_CHECK(pid3[3] == 'b');
	TEST_CHECK(pid3[4] == 'a');
	TEST_CHECK(pid3[5] == 'r');
}

TORRENT_TEST(init_dht)
{
	auto count_dht_inits = [](session& ses)
	{
		int count = 0;
		int num = 70; // this number is adjusted per version, an estimate
		time_point const end_time = clock_type::now() + seconds(15);
		while (true)
		{
			time_point const now = clock_type::now();
			if (now > end_time) return count;

			ses.wait_for_alert(end_time - now);
			std::vector<alert*> alerts;
			ses.pop_alerts(&alerts);
			for (auto a : alerts)
			{
				std::printf("%d: [%s] %s\n", num, a->what(), a->message().c_str());
				if (a->type() == log_alert::alert_type)
				{
					std::string const msg = a->message();
					if (msg.find("about to start DHT") != std::string::npos)
						count++;
				}
				num--;
			}
			if (num <= 0) return count;
		}
		return count;
	};

	{
		settings_pack p = settings();
		p.set_bool(settings_pack::enable_dht, true);
		p.set_int(settings_pack::alert_mask, alert::all_categories);
		// default value
		p.set_str(settings_pack::dht_bootstrap_nodes, "dht.libtorrent.org:25401");

		lt::session s(p);

		int const count = count_dht_inits(s);
		TEST_EQUAL(count, 1);
	}

	{
		settings_pack p = settings();
		p.set_bool(settings_pack::enable_dht, true);
		p.set_int(settings_pack::alert_mask, alert::all_categories);
		// no default value
		p.set_str(settings_pack::dht_bootstrap_nodes, "test.libtorrent.org:25401:8888");

		lt::session s(p);

		int const count = count_dht_inits(s);
		TEST_EQUAL(count, 1);
	}

	{
		settings_pack p = settings();
		p.set_bool(settings_pack::enable_dht, true);
		p.set_int(settings_pack::alert_mask, alert::all_categories);
		// empty value
		p.set_str(settings_pack::dht_bootstrap_nodes, "");

		lt::session s(p);

		int const count = count_dht_inits(s);
		TEST_EQUAL(count, 1);
	}
}
