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
	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);

	// try add 600 torrent_add_alert to make sure we honor the limit of 500
	// alerts.
	for (int i = 0; i < 600; ++i)
		mgr.emplace_alert<torrent_finished_alert>(torrent_handle());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	// even though we posted 600, the limit was 500
	TEST_EQUAL(alerts.size(), 500);

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);

	// now, try lowering the limit and do the same thing again
	mgr.set_alert_queue_size_limit(200);

	for (int i = 0; i < 600; ++i)
		mgr.emplace_alert<torrent_finished_alert>(torrent_handle());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);

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
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

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
	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);

	mgr.set_dispatch_function(boost::bind(&test_dispatch_fun, boost::ref(cnt), _1));

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);

	TEST_EQUAL(cnt, 20);

	for (int i = 0; i < 200; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);
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
	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);

	// if there are queued alerts when we set the notify function,
	// that counts as an edge and it's called
	mgr.set_notify_function(boost::bind(&test_notify_fun, boost::ref(cnt)));

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);
	TEST_EQUAL(cnt, 1);

	// subsequent posted alerts will not cause an edge (because there are
	// already alerts queued)
	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);
	TEST_EQUAL(cnt, 1);

	// however, if we pop all the alerts and post new ones, there will be
	// and edge triggering the notify call
	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	TEST_EQUAL(mgr.wait_for_alert(milliseconds(0)) != NULL, true);
	TEST_EQUAL(cnt, 2);
}

#ifndef TORRENT_DISABLE_EXTENSIONS
int plugin_alerts[3] = { 0, 0, 0 };

struct test_plugin : libtorrent::plugin
{
	test_plugin(int index, bool reliable_alerts = false) : m_index(index),
		m_features(reliable_alerts ? libtorrent::plugin::reliable_alerts_feature  : 0) {}
	boost::uint32_t implemented_features() { return m_features; }
	virtual void on_alert(alert const* a)
	{
		++plugin_alerts[m_index];
	}

	int m_index;
	boost::uint32_t m_features;
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

TORRENT_TEST(reliable_alerts)
{
#ifndef TORRENT_DISABLE_EXTENSIONS
	memset(plugin_alerts, 0, sizeof(plugin_alerts));
	alert_manager mgr(100, 0xffffffff);

	mgr.add_extension(boost::make_shared<test_plugin>(0));
	mgr.add_extension(boost::make_shared<test_plugin>(1));
	mgr.add_extension(boost::make_shared<test_plugin>(2, true));

	for (int i = 0; i < 105; ++i)
		mgr.emplace_alert<torrent_finished_alert>(torrent_handle());

	TEST_EQUAL(plugin_alerts[0], 100);
	TEST_EQUAL(plugin_alerts[1], 100);
	TEST_EQUAL(plugin_alerts[2], 105);
#endif
}

void alert_emplacer(alert_manager& mgr, boost::atomic<bool> &running)
{
	// There is a memory barrier on this check. As well as any mutex
	// access, IO calls, indirect function calls, etc.
	while (running)
	{
		// The load for this call must happen before this test because
		// because the compiler cannot move it in a way to would break
		// a single threaded program.
		if (mgr.should_post<torrent_finished_alert>())
		{
			test_sleep(1);	// simulate work done in between
			mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
		}

		// The load for this check may get optimized out when the bit is
		// set to 0 on the above test. When the bit is 1 the load would be
		// done after the emplace_alert call above.
		if (mgr.should_post<torrent_finished_alert>())
		{
			test_sleep(1);
			mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
		}

		// Likewise, if the bit was 0 on the above test this entire loop can
		// get optimized out. If it was 1 the load will be done for every
		// iteration of the loop.
		for (int i = 0; i < 10; i++)
		{
			if (mgr.should_post<torrent_finished_alert>())
			{
				test_sleep(1);
				mgr.emplace_alert<torrent_finished_alert>(torrent_handle());
			}
		}
	}
}

TORRENT_TEST(alert_mask_response)
{
	const int n_threads = 10;
	std::vector<alert*> alerts;
	boost::atomic<bool> emplacer_running(true);
	alert_manager mgr(100, lt::alert::status_notification);
	std::vector<boost::shared_ptr<libtorrent::thread>> threads;

	// start some threads to emplace alerts
	for (int i = 0; i < n_threads; i++)
		threads.push_back(boost::shared_ptr<libtorrent::thread>(new libtorrent::thread(boost::bind(
			&alert_emplacer, boost::ref(mgr), boost::ref(emplacer_running)))));

	// we need as many iterations as possible because the test
	// is racy since we must pop alerts after clearing the mask.
	for (int i = 0; i < 1000; ++i)
	{
		// Clear the alerts mask, pop alerts and wait. We may still
		// get one alert per thread after the update if it takes
		// place after should_post() but before emplace_alert()
		mgr.set_alert_mask(0);
		mgr.get_all(alerts);
		test_sleep(n_threads * 5);
		mgr.get_all(alerts);
		TEST_EQUAL(alerts.size() <= n_threads, true);

		// Reset the bit to 1 and wait for the next alert.
		// It looks like this only shows that libtorrent
		// can take any amount of time to respond (which is true)
		// but the fact that responds at all shows that it will respond
		// to 0 to 1 changes at least once inside the loop. How long
		// that will take is impossible to determine because we cannot
		// make any assumptions about the scheduler, so to make the
		// test deterministic we need to wait forever.
		mgr.set_alert_mask(lt::alert::status_notification);
		while (mgr.wait_for_alert(milliseconds(200)) == NULL);
	}

	// kill all threads
	emplacer_running = false;
	for (int i = 0; i < n_threads; i++)
		threads[i]->join();
}

TORRENT_TEST(thread_safety)
{
	const int n_threads = 50;
	std::vector<alert*> alerts;
	boost::atomic<bool> emplacer_running(true);
	alert_manager mgr(100, lt::alert::status_notification);
	std::vector<boost::shared_ptr<libtorrent::thread>> threads;

	// start some threads to emplace alerts
	for (int i = 0; i < n_threads; i++)
		threads.push_back(boost::shared_ptr<libtorrent::thread>(new libtorrent::thread(boost::bind(
			&alert_emplacer, boost::ref(mgr), boost::ref(emplacer_running)))));

	// this test needs a lot of work. for now all it will
	// do is trigger an assert on a debug build in do_emplace_alert()
	// if alerts are popped twice in the timespan between alert
	// construction and it being available to get_all().
	// A proper test needs to at least emplace alerts with allocated
	// strings and verify them.
	for (int i = 0; i < 1000; ++i)
		mgr.get_all(alerts);

	// kill all threads
	emplacer_running = false;
	for (int i = 0; i < n_threads; i++)
		threads[i]->join();
}


void post_torrent_added(alert_manager* mgr)
{
	test_sleep(10);
	mgr->emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());
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

	mgr.emplace_alert<add_torrent_alert>(torrent_handle(), add_torrent_params(), error_code());

	start = clock_type::now();
	a = mgr.wait_for_alert(seconds(1));
	end = clock_type::now();

	fprintf(stderr, "delay: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(1));
	TEST_CHECK(a->type() == add_torrent_alert::alert_type);

	std::vector<alert*> alerts;
	mgr.get_all(alerts);

	start = clock_type::now();
	libtorrent::thread posting_thread(boost::bind(&post_torrent_added, &mgr));

	a = mgr.wait_for_alert(seconds(10));
	end = clock_type::now();

	fprintf(stderr, "delay: %d ms\n", int(total_milliseconds(end - start)));
	TEST_CHECK(end - start < milliseconds(500));
	TEST_CHECK(a->type() == add_torrent_alert::alert_type);

	posting_thread.join();
}

TORRENT_TEST(alert_mask)
{
	alert_manager mgr(100, 0xffffffff);

	TEST_CHECK(mgr.should_post<add_torrent_alert>());
	TEST_CHECK(mgr.should_post<torrent_paused_alert>());

	mgr.set_alert_mask(0);

	TEST_CHECK(!mgr.should_post<add_torrent_alert>());
	TEST_CHECK(!mgr.should_post<torrent_paused_alert>());
}

