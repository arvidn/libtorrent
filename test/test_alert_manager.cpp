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
#include "libtorrent/thread.hpp"
#include "setup_transfer.hpp"

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

using namespace libtorrent;

TORRENT_TEST(limit)
{
	alert_manager mgr(500, 0xffffffff);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 500);
	TEST_EQUAL(mgr.pending(), false);

	// try add 600 torrent_add_alert to make sure we honor the limit of 500
	// alerts.
	for (int i = 0; i < 600; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	// even though we posted 600, the limit was 500
	TEST_EQUAL(alerts.size(), 500);

	TEST_EQUAL(mgr.pending(), false);

	// now, try lowering the limit and do the same thing again
	mgr.set_alert_queue_size_limit(200);

	for (int i = 0; i < 600; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);

	mgr.get_all(alerts);

	// even though we posted 600, the limit was 200
	TEST_EQUAL(alerts.size(), 200);
}

TORRENT_TEST(priority_limit)
{
	alert_manager mgr(100, 0xffffffff);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 100);

	// this should only add 100 because of the limit
	for (int i = 0; i < 200; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	// the limit is twice as high for priority alerts
	for (int i = 0; i < 200; ++i)
		mgr.emplace_alert<file_rename_failed_alert>(torrent_handle(), i, error_code());

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	// even though we posted 400, the limit was 100 for half of them and
	// 200 for the other half, meaning we should have 200 alerts now
	TEST_EQUAL(alerts.size(), 200);
}

void test_dispatch_fun(int& cnt, std::auto_ptr<alert> const& a)
{
	++cnt;
}

TORRENT_TEST(dispatch_function)
{
#ifndef TORRENT_NO_DEPRECATE
	int cnt = 0;
	alert_manager mgr(100, 0xffffffff);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 100);
	TEST_EQUAL(mgr.pending(), false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);

	mgr.set_dispatch_function(boost::bind(&test_dispatch_fun, boost::ref(cnt), _1));

	TEST_EQUAL(mgr.pending(), false);

	TEST_EQUAL(cnt, 20);

	for (int i = 0; i < 200; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), false);
	TEST_EQUAL(cnt, 220);
#endif
}

void test_notify_fun(int& cnt)
{
	++cnt;
}

TORRENT_TEST(notify_function)
{
	int cnt = 0;
	alert_manager mgr(100, 0xffffffff);

	TEST_EQUAL(mgr.alert_queue_size_limit(), 100);
	TEST_EQUAL(mgr.pending(), false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);

	// if there are queued alerts when we set the notify function,
	// that counts as an edge and it's called
	mgr.set_notify_function(boost::bind(&test_notify_fun, boost::ref(cnt)));

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 1);

	// subsequent posted alerts will not cause an edge (because there are
	// already alerts queued)
	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 1);

	// however, if we pop all the alerts and post new ones, there will be
	// and edge triggering the notify call
	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	TEST_EQUAL(mgr.pending(), false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 2);
}

#ifndef TORRENT_DISABLE_EXTENSIONS
int plugin_alerts[3] = { 0, 0, 0 };

struct test_plugin : libtorrent::plugin
{
	test_plugin(int index) : m_index(index) {}
	virtual void on_alert(alert const* a)
	{
		++plugin_alerts[m_index];
	}
	int m_index;
};

#endif

TORRENT_TEST(extensions)
{
#ifndef TORRENT_DISABLE_EXTENSIONS
	memset(plugin_alerts, 0, sizeof(plugin_alerts));
	alert_manager mgr(100, 0xffffffff);

	mgr.add_extension(boost::make_shared<test_plugin>(0));
	mgr.add_extension(boost::make_shared<test_plugin>(1));
	mgr.add_extension(boost::make_shared<test_plugin>(2));

	for (int i = 0; i < 53; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(plugin_alerts[0], 53);
	TEST_EQUAL(plugin_alerts[1], 53);
	TEST_EQUAL(plugin_alerts[2], 53);

	for (int i = 0; i < 17; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(plugin_alerts[0], 70);
	TEST_EQUAL(plugin_alerts[1], 70);
	TEST_EQUAL(plugin_alerts[2], 70);
#endif
}

void post_torrent_added(alert_manager* mgr)
{
	test_sleep(10);
	mgr->emplace_alert<torrent_added_alert>(torrent_handle());
}

TORRENT_TEST(wait_for_alert)
{
	alert_manager mgr(100, 0xffffffff);

	time_point start = clock_type::now();

	alert* a = mgr.wait_for_alert(seconds(1));

	time_point end = clock_type::now();
	TEST_EQUAL(a, static_cast<alert*>(0));
	fprintf(stderr, "delay: %d ms (expected 1 second)\n"
		, int(total_milliseconds(end - start)));
	TEST_CHECK(end - start > milliseconds(900));
	TEST_CHECK(end - start < milliseconds(1100));

	mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	start = clock_type::now();
	a = mgr.wait_for_alert(seconds(1));
	end = clock_type::now();

	fprintf(stderr, "delay: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(1));
	TEST_CHECK(a->type() == torrent_added_alert::alert_type);

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	start = clock_type::now();
	thread posting_thread(boost::bind(&post_torrent_added, &mgr));

	a = mgr.wait_for_alert(seconds(10));
	end = clock_type::now();

	fprintf(stderr, "delay: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(500));
	TEST_CHECK(a->type() == torrent_added_alert::alert_type);

	posting_thread.join();
}

TORRENT_TEST(alert_mask)
{
	alert_manager mgr(100, 0xffffffff);

	TEST_CHECK(mgr.should_post<torrent_added_alert>());
	TEST_CHECK(mgr.should_post<torrent_paused_alert>());

	mgr.set_alert_mask(0);

	TEST_CHECK(!mgr.should_post<torrent_added_alert>());
	TEST_CHECK(!mgr.should_post<torrent_paused_alert>());
}

