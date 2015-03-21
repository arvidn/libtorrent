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

#include "test.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/thread.hpp"

#include <boost/bind.hpp>

using namespace libtorrent;

void check_timer_loop(mutex& m, ptime& last, condition_variable& cv)
{
	mutex::scoped_lock l(m);
	cv.wait(l);
	l.unlock();

	for (int i = 0; i < 10000; ++i)
	{
		mutex::scoped_lock l(m);
		ptime now = time_now_hires();
		TEST_CHECK(now >= last);
		last = now;
	}
}

int test_main()
{

	// make sure the time classes have correct semantics

	TEST_EQUAL(total_milliseconds(milliseconds(100)), 100);
	TEST_EQUAL(total_milliseconds(milliseconds(1)),  1);
	TEST_EQUAL(total_milliseconds(seconds(1)), 1000);
	TEST_EQUAL(total_seconds(minutes(1)), 60);
	TEST_EQUAL(total_seconds(hours(1)), 3600);

	// make sure it doesn't wrap at 32 bit arithmetic
	TEST_EQUAL(total_seconds(seconds(281474976)), 281474976);
	TEST_EQUAL(total_milliseconds(milliseconds(281474976)), 281474976);

	// make sure the timer is monotonic

	ptime now = time_now_hires();
	ptime last = now;
	for (int i = 0; i < 1000; ++i)
	{
		now = time_now_hires();
		TEST_CHECK(now >= last);
		last = now;
	}
	
	mutex m;
	condition_variable cv;
	thread t1(boost::bind(&check_timer_loop, boost::ref(m), boost::ref(last), boost::ref(cv)));
	thread t2(boost::bind(&check_timer_loop, boost::ref(m), boost::ref(last), boost::ref(cv)));
	thread t3(boost::bind(&check_timer_loop, boost::ref(m), boost::ref(last), boost::ref(cv)));
	thread t4(boost::bind(&check_timer_loop, boost::ref(m), boost::ref(last), boost::ref(cv)));

	sleep(100);

	cv.notify_all();

	t1.join();
	t2.join();
	t3.join();
	t4.join();

	return 0;
}

