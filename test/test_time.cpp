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

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace lt;

namespace {

void check_timer_loop(std::mutex& m, time_point& last, std::condition_variable& cv)
{
	std::unique_lock<std::mutex> l(m);
	cv.wait(l);
	l.unlock();

	for (int i = 0; i < 10000; ++i)
	{
		std::lock_guard<std::mutex> ll(m);
		time_point now = clock_type::now();
		TEST_CHECK(now >= last);
		last = now;
	}
}

} // anonymous namespace

TORRENT_TEST(time)
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

	time_point now = clock_type::now();
	time_point last = now;
	for (int i = 0; i < 1000; ++i)
	{
		now = clock_type::now();
		TEST_CHECK(now >= last);
		last = now;
	}

	std::mutex m;
	std::condition_variable cv;
	std::thread t1(&check_timer_loop, std::ref(m), std::ref(last), std::ref(cv));
	std::thread t2(&check_timer_loop, std::ref(m), std::ref(last), std::ref(cv));
	std::thread t3(&check_timer_loop, std::ref(m), std::ref(last), std::ref(cv));
	std::thread t4(&check_timer_loop, std::ref(m), std::ref(last), std::ref(cv));

	std::this_thread::sleep_for(lt::milliseconds(100));

	cv.notify_all();

	t1.join();
	t2.join();
	t3.join();
	t4.join();
}

