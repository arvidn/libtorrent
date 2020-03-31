/*

Copyright (c) 2015, Arvid Norberg
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
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions.hpp"
#include "setup_transfer.hpp"

#include <functional>
#include <thread>

using namespace lt;

TORRENT_TEST(limit)
{
	alert_manager mgr(500, alert_category::all);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 500);
	TEST_EQUAL(mgr.pending(), false);

	// try add 600 torrent_add_alert to make sure we honor the limit of 500
	// alerts.
	for (piece_index_t i{0}; i < piece_index_t{600}; ++i)
		mgr.emplace_alert<piece_finished_alert>(torrent_handle(), i);

	TEST_EQUAL(mgr.pending(), true);

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	// even though we posted 600, the limit was 500
	// +1 for the alerts_dropped_alert
	TEST_EQUAL(alerts.size(), 501);

	TEST_EQUAL(mgr.pending(), false);

	// now, try lowering the limit and do the same thing again
	mgr.set_alert_queue_size_limit(200);

	for (piece_index_t i{0}; i < piece_index_t{600}; ++i)
		mgr.emplace_alert<piece_finished_alert>(torrent_handle(), i);

	TEST_EQUAL(mgr.pending(), true);

	mgr.get_all(alerts);

	// even though we posted 600, the limit was 200
	// +1 for the alerts_dropped_alert
	TEST_EQUAL(alerts.size(), 201);
}

TORRENT_TEST(limit_int_max)
{
	int const inf = std::numeric_limits<int>::max();
	alert_manager mgr(inf, alert_category::all);

	TEST_EQUAL(mgr.alert_queue_size_limit(), inf);

	for (piece_index_t i{0}; i < piece_index_t{600}; ++i)
		mgr.emplace_alert<piece_finished_alert>(torrent_handle(), i);

	for (piece_index_t i{0}; i < piece_index_t{600}; ++i)
		mgr.emplace_alert<torrent_removed_alert>(torrent_handle(), sha1_hash());

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	TEST_EQUAL(alerts.size(), 1200);
}

TORRENT_TEST(priority_limit)
{
	alert_manager mgr(100, alert_category::all);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 100);

	// this should only add 100 because of the limit
	for (piece_index_t i{0}; i < piece_index_t{200}; ++i)
		mgr.emplace_alert<piece_finished_alert>(torrent_handle(), i);

	// the limit is twice as high for priority alerts
	for (file_index_t i(0); i < file_index_t(300); ++i)
		mgr.emplace_alert<file_rename_failed_alert>(torrent_handle(), i, error_code());

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	// even though we posted 500, the limit was 100 for half of them and
	// 100 + 200 for the other half, meaning we should have 300 alerts now
	// +1 for the alerts_dropped_alert
	TEST_EQUAL(alerts.size(), 301);
}

namespace {
void test_notify_fun(int& cnt)
{
	++cnt;
}
} // anonymous namespace

TORRENT_TEST(notify_function)
{
	int cnt = 0;
	alert_manager mgr(100, alert_category::all);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 100);
	TEST_EQUAL(mgr.pending(), false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.pending(), true);

	// if there are queued alerts when we set the notify function,
	// that counts as an edge and it's called
	mgr.set_notify_function(std::bind(&test_notify_fun, std::ref(cnt)));

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 1);

	// subsequent posted alerts will not cause an edge (because there are
	// already alerts queued)
	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 1);

	// however, if we pop all the alerts and post new ones, there will be
	// and edge triggering the notify call
	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	TEST_EQUAL(mgr.pending(), false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 2);
}

#ifndef TORRENT_DISABLE_EXTENSIONS
namespace {
int plugin_alerts[3] = { 0, 0, 0 };

struct test_plugin : lt::plugin
{
	explicit test_plugin(int index) : m_index(index) {}
	void on_alert(alert const*) override
	{
		++plugin_alerts[m_index];
	}
	int m_index;
};
} // anonymous namespace
#endif

TORRENT_TEST(extensions)
{
#ifndef TORRENT_DISABLE_EXTENSIONS
	memset(plugin_alerts, 0, sizeof(plugin_alerts));
	alert_manager mgr(100, alert_category::all);

	mgr.add_extension(std::make_shared<test_plugin>(0));
	mgr.add_extension(std::make_shared<test_plugin>(1));
	mgr.add_extension(std::make_shared<test_plugin>(2));

	for (int i = 0; i < 53; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(plugin_alerts[0], 53);
	TEST_EQUAL(plugin_alerts[1], 53);
	TEST_EQUAL(plugin_alerts[2], 53);

	for (int i = 0; i < 17; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(plugin_alerts[0], 70);
	TEST_EQUAL(plugin_alerts[1], 70);
	TEST_EQUAL(plugin_alerts[2], 70);
#endif
}

/*
namespace {

void post_torrent_added(alert_manager* mgr)
{
	std::this_thread::sleep_for(lt::milliseconds(10));
	mgr->emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());
}

} // anonymous namespace

// this test is too flaky

TORRENT_TEST(wait_for_alert)
{
	alert_manager mgr(100, alert_category::all);

	time_point start = clock_type::now();

	alert* a = mgr.wait_for_alert(seconds(1));

	time_point end = clock_type::now();
	TEST_EQUAL(a, static_cast<alert*>(nullptr));
	std::printf("delay: %d ms (expected 1 second)\n"
		, int(total_milliseconds(end - start)));
	TEST_CHECK(end - start > milliseconds(900));
	TEST_CHECK(end - start < milliseconds(1100));

	mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	start = clock_type::now();
	a = mgr.wait_for_alert(seconds(1));
	end = clock_type::now();

	std::printf("delay: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(1));
	TEST_CHECK(a->type() == add_torrent_alert::alert_type);

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	start = clock_type::now();
	std::thread posting_thread(&post_torrent_added, &mgr);

	a = mgr.wait_for_alert(seconds(10));
	end = clock_type::now();

	std::printf("delay: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(500));
	TEST_CHECK(a->type() == add_torrent_alert::alert_type);

	posting_thread.join();
}
*/

TORRENT_TEST(alert_mask)
{
	alert_manager mgr(100, alert_category::all);

	TEST_CHECK(mgr.should_post<add_torrent_alert>());
	TEST_CHECK(mgr.should_post<torrent_paused_alert>());

	mgr.set_alert_mask({});

	TEST_CHECK(!mgr.should_post<add_torrent_alert>());
	TEST_CHECK(!mgr.should_post<torrent_paused_alert>());
}

TORRENT_TEST(dropped_alerts)
{
	alert_manager mgr(1, alert_category::all);

	// nothing has dropped yet
	mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
	// still nothing, there's space for one alert
	mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
	// still nothing, there's space for one alert
	mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
	// that last alert got dropped though, since it would have brought the queue
	// size to 3
	std::vector<alert*> alerts;
	mgr.get_all(alerts);
	auto const d = alert_cast<alerts_dropped_alert>(alerts.back())->dropped_alerts;
	TEST_EQUAL(d.count(), 1);
	TEST_CHECK(d.test(torrent_finished_alert::alert_type));
}

TORRENT_TEST(alerts_dropped_alert)
{
	alert_manager mgr(1, alert_category::all);

	mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
	mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
	mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
	// that last alert got dropped though, since it would have brought the queue
	// size to 3
	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	TEST_EQUAL(alerts.back()->message(), "dropped alerts: torrent_finished ");
}

#ifndef TORRENT_DISABLE_EXTENSIONS
struct post_plugin : lt::plugin
{
	explicit post_plugin(alert_manager& m) : mgr(m) {}
	void on_alert(alert const*)
	{
		if (++depth > 10) return;
		mgr.emplace_alert<piece_finished_alert>(torrent_handle(), piece_index_t{0});
	}

	alert_manager& mgr;
	int depth = 0;
};

// make sure the alert manager supports alerts being posted while executing a
// plugin handler
TORRENT_TEST(recursive_alerts)
{
	alert_manager mgr(100, alert_category::all);
	auto pl = std::make_shared<post_plugin>(mgr);
	mgr.add_extension(pl);

	mgr.emplace_alert<piece_finished_alert>(torrent_handle(), piece_index_t{0});

	TEST_EQUAL(pl->depth, 11);
}

#endif // TORRENT_DISABLE_EXTENSIONS

