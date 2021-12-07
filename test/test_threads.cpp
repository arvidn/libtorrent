/*

Copyright (c) 2010, 2013, 2015-2017, 2019-2021, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

