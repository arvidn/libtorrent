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

#include <boost/bind.hpp>

using namespace libtorrent;

void test_limit()
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
	int num_resume;
	mgr.get_all(alerts, num_resume);

	// even though we posted 600, the limit was 500
	TEST_EQUAL(alerts.size(), 500);

	TEST_EQUAL(mgr.pending(), false);
}

void test_priority_limit()
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
	int num_resume;
	mgr.get_all(alerts, num_resume);

	// even though we posted 400, the limit was 100 for half of them and
	// 200 for the other half, meaning we should have 200 alerts now
	TEST_EQUAL(alerts.size(), 200);
}

void test_dispatch_fun(int& cnt, std::auto_ptr<alert> a)
{
	++cnt;
}

void test_dispatch_function()
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

void test_notify_function()
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
	int num_resume;
	mgr.get_all(alerts, num_resume);

	TEST_EQUAL(mgr.pending(), false);

	for (int i = 0; i < 20; ++i)
		mgr.emplace_alert<torrent_added_alert>(torrent_handle());

	TEST_EQUAL(mgr.pending(), true);
	TEST_EQUAL(cnt, 2);
}

int test_main()
{
	test_limit();
	test_priority_limit();
	test_dispatch_function();
	test_notify_function();

	// TODO: test wait_for_alert
	// TODO: test num_queued_resume
	// TODO: test alert_mask

	return 0;
}

