/*

Copyright (c) 2010, Arvid Norberg
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

#include <functional>
#include <atomic>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "test.hpp"
#include "libtorrent/time.hpp"

using namespace lt;

namespace {

void fun(std::condition_variable* s, std::mutex* m, int* waiting, int i)
{
	std::printf("thread %d waiting\n", i);
	std::unique_lock<std::mutex> l(*m);
	*waiting += 1;
	s->wait(l);
	std::printf("thread %d done\n", i);
}

void increment(std::condition_variable* s, std::mutex* m, int* waiting, std::atomic<int>* c)
{
	std::unique_lock<std::mutex> l(*m);
	*waiting += 1;
	s->wait(l);
	l.unlock();
	for (int i = 0; i < 1000000; ++i)
		++*c;
}

void decrement(std::condition_variable* s, std::mutex* m, int* waiting, std::atomic<int>* c)
{
	std::unique_lock<std::mutex> l(*m);
	*waiting += 1;
	s->wait(l);
	l.unlock();
	for (int i = 0; i < 1000000; ++i)
		--*c;
}

} // anonymous namespace

TORRENT_TEST(threads)
{
	std::condition_variable cond;
	std::mutex m;
	std::vector<std::thread> threads;
	int waiting = 0;
	for (int i = 0; i < 20; ++i)
	{
		threads.emplace_back(&fun, &cond, &m, &waiting, i);
	}

	// make sure all threads are waiting on the condition_variable
	std::unique_lock<std::mutex> l(m);
	while (waiting < 20)
	{
		l.unlock();
		std::this_thread::sleep_for(lt::milliseconds(10));
		l.lock();
	}

	cond.notify_all();
	l.unlock();

	for (auto& t : threads) t.join();
	threads.clear();

	waiting = 0;
	std::atomic<int> c(0);
	for (int i = 0; i < 3; ++i)
	{
		threads.emplace_back(&increment, &cond, &m, &waiting, &c);
		threads.emplace_back(&decrement, &cond, &m, &waiting, &c);
	}

	// make sure all threads are waiting on the condition_variable
	l.lock();
	while (waiting < 6)
	{
		l.unlock();
		std::this_thread::sleep_for(lt::milliseconds(10));
		l.lock();
	}

	cond.notify_all();
	l.unlock();

	for (auto& t : threads) t.join();

	TEST_CHECK(c == 0);
}

