/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/disk_tick_thread.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/time.hpp"

#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>

using namespace lt;
using namespace std::chrono_literals;

using aux::steady_clock;

namespace {

template <typename F>
bool wait_for(F f)
{
	for (int i = 0; i < 500; ++i)
	{
		if (f())
			return true;
		std::this_thread::sleep_for(10ms);
	}
	return f();
}

}

TORRENT_TEST(periodic_task_reschedules)
{
	std::atomic<int> runs{0};
	io_context ios;
	aux::disk_tick_thread t(ios);
	t.add([&](steady_clock::time_point const now) {
		++runs;
		return now + 20ms;
	});
	TEST_CHECK(wait_for([&] { return runs >= 3; }));
}

TORRENT_TEST(oneshot_runs_once_at_deadline)
{
	std::atomic<int> runs{0};
	io_context ios;
	aux::disk_tick_thread t(ios);
	auto const start = steady_clock::now();
	t.schedule(start + 50ms, [&] { ++runs; });
	TEST_CHECK(wait_for([&] { return runs == 1; }));
	TEST_CHECK(steady_clock::now() - start >= 50ms);
	std::this_thread::sleep_for(100ms);
	TEST_EQUAL(runs.load(), 1);
}

TORRENT_TEST(oneshots_run_in_deadline_order)
{
	std::vector<int> order;
	std::mutex m;
	io_context ios;
	aux::disk_tick_thread t(ios);
	auto const start = steady_clock::now();
	for (int const i : {3, 1, 2})
	{
		t.schedule(start + i * 30ms, [&, i] {
			std::lock_guard<std::mutex> l(m);
			order.push_back(i);
		});
	}
	TEST_CHECK(wait_for([&] {
		std::lock_guard<std::mutex> l(m);
		return order.size() == 3;
	}));
	std::lock_guard<std::mutex> l(m);
	TEST_CHECK((order == std::vector<int>{1, 2, 3}));
}

// independent of timing: all entries are pushed while the thread is blocked in
// a task, with deadlines already in the past, so they're popped back to back
// in heap order
TORRENT_TEST(oneshot_heap_order)
{
	std::vector<int> order;
	std::mutex m;
	std::atomic<bool> started{false};
	std::atomic<bool> release{false};
	io_context ios;
	aux::disk_tick_thread t(ios);
	t.schedule(steady_clock::now(), [&] {
		started = true;
		while (!release)
			std::this_thread::sleep_for(1ms);
	});
	TEST_CHECK(wait_for([&] { return started.load(); }));

	int const n = 100;
	std::vector<int> deadlines(n);
	std::iota(deadlines.begin(), deadlines.end(), 0);
	aux::random_shuffle(deadlines);

	auto const base = steady_clock::now() - 1h;
	for (int const i : deadlines)
	{
		t.schedule(base + i * 1ms, [&, i] {
			std::lock_guard<std::mutex> l(m);
			order.push_back(i);
		});
	}

	release = true;
	TEST_CHECK(wait_for([&] {
		std::lock_guard<std::mutex> l(m);
		return int(order.size()) == n;
	}));

	std::vector<int> expected(n);
	std::iota(expected.begin(), expected.end(), 0);
	std::lock_guard<std::mutex> l(m);
	TEST_CHECK(order == expected);
}

TORRENT_TEST(tasks_run_one_at_a_time)
{
	std::atomic<bool> running{false};
	std::atomic<bool> overlap{false};
	std::atomic<int> runs{0};
	io_context ios;
	aux::disk_tick_thread t(ios);
	auto const task = [&](steady_clock::time_point const now) {
		if (running.exchange(true))
			overlap = true;
		std::this_thread::sleep_for(2ms);
		running = false;
		++runs;
		return now + 1ms;
	};
	t.add(task);
	t.add(task);
	TEST_CHECK(wait_for([&] { return runs >= 10; }));
	TEST_CHECK(!overlap);
}

TORRENT_TEST(abort_with_pending_work)
{
	io_context ios;
	aux::disk_tick_thread t(ios);
	t.schedule(steady_clock::now() + 1h, [] {});
	t.add([](steady_clock::time_point const now) { return now + 1h; });
	auto const start = steady_clock::now();
	t.join();
	TEST_CHECK(steady_clock::now() - start < 5s);
	// idempotent
	t.join();
}

TORRENT_TEST(abort_drops_pending_tasks)
{
	auto const token = std::make_shared<int>(0);
	io_context ios;
	aux::disk_tick_thread t(ios);
	t.schedule(steady_clock::now() + 1h, [token] {});
	t.add([token](steady_clock::time_point const now) { return now + 1h; });
	// the periodic task has run once by now (or is about to), either way it
	// holds a reference
	TEST_CHECK(token.use_count() >= 2);
	t.abort();
	// the periodic task may be mid-run, in which case the thread's copy
	// goes away when it returns. Either way, no join is needed
	TEST_CHECK(wait_for([&] { return token.use_count() == 1; }));
	t.join();
}

TORRENT_TEST(abort_does_not_wait_for_running_task)
{
	std::atomic<bool> started{false};
	std::atomic<bool> release{false};
	io_context ios;
	aux::disk_tick_thread t(ios);
	t.add([&](steady_clock::time_point const now) {
		started = true;
		while (!release)
			std::this_thread::sleep_for(1ms);
		return now + 1h;
	});
	TEST_CHECK(wait_for([&] { return started.load(); }));
	// the task stays blocked until released below, so a joining abort()
	// would deadlock here
	t.abort();
	release = true;
	t.join();
}
