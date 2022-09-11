/*

Copyright (c) 2016, 2019-2021, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp"
#include "libtorrent/aux_/disk_job.hpp"
#include "libtorrent/io_context.hpp"
#include <condition_variable>
#include <thread>
#include <chrono>

using lt::io_context;

using namespace std::chrono_literals;

std::mutex g_job_mutex;

void thread_fun(lt::aux::disk_io_thread_pool& pool, lt::executor_work_guard<io_context::executor_type>)
{
	std::unique_lock<std::mutex> l(g_job_mutex);
	for (;;)
	{
		bool const should_exit = pool.wait_for_job(l);
		if (should_exit) break;
		lt::aux::disk_job* j = static_cast<lt::aux::disk_job*>(pool.pop_front());
		l.unlock();

		// pretend to perform job
		TORRENT_UNUSED(j);
		std::this_thread::sleep_for(1ms);

		l.lock();
	}
}
/*
TORRENT_TEST(disk_io_thread_pool_idle_reaping)
{
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	test_threads threads;
	sim::asio::io_context ios(sim);
	lt::aux::disk_io_thread_pool pool(threads, ios);
	threads.m_pool = &pool;
	pool.set_max_threads(3);
	pool.job_queued(3);
	TEST_EQUAL(pool.num_threads(), 3);
	// make sure all the threads are up and settled in the active state
	threads.set_active_threads(3);

	// first just kill one thread
	threads.set_active_threads(2);
	lt::aux::deadline_timer idle_delay(ios);
	// the thread will be killed the second time the reaper runs and we need
	// to wait one extra minute to make sure the check runs after the reaper
	idle_delay.expires_after(std::chrono::minutes(3));
	idle_delay.async_wait([&](lt::error_code const&)
	{
		// this is a kludge to work around a race between the thread
		// exiting and checking the number of threads
		// in production we only check num_threads from the disk I/O threads
		// so there are no race problems there
		threads.wait_for_thread_exit(2);
		TEST_EQUAL(pool.num_threads(), 2);
		sim.stop();
	});
	sim.run();
	sim.restart();

	// now kill the rest
	threads.set_active_threads(0);
	idle_delay.expires_after(std::chrono::minutes(3));
	idle_delay.async_wait([&](lt::error_code const&)
	{
		// see comment above about this kludge
		threads.wait_for_thread_exit(0);
		TEST_EQUAL(pool.num_threads(), 0);
	});
	sim.run();
}
*/

TORRENT_TEST(disk_io_thread_pool_abort_wait)
{
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	sim::asio::io_context ios(sim);
	lt::aux::disk_io_thread_pool pool(&thread_fun, ios);
	pool.set_max_threads(3);
	lt::aux::disk_job jobs[3];

	{
		std::unique_lock<std::mutex> l(g_job_mutex);
		for (auto& j : jobs)
			pool.push_back(&j);
		pool.submit_jobs();
	}
	TEST_EQUAL(pool.num_threads(), 3);
	pool.abort(true);
	TEST_EQUAL(pool.num_threads(), 0);
}

#if 0
// disabled for now because io_context::work doesn't work under the simulator
// and we need it to stop this test from exiting prematurely
TORRENT_TEST(disk_io_thread_pool_abort_no_wait)
{
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	sim::asio::io_context ios(sim);
	lt::aux::disk_io_thread_pool pool(&thread_fun, ios);
	pool.set_max_threads(3);
	pool.job_queued(3);
	TEST_EQUAL(pool.num_threads(), 3);
	pool.abort(false);
	TEST_EQUAL(pool.num_threads(), 0);
	sim.run();
}
#endif

TORRENT_TEST(disk_io_thread_pool_max_threads)
{
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	sim::asio::io_context ios(sim);
	lt::aux::disk_io_thread_pool pool(thread_fun, ios);
	// first check that the thread limit is respected when adding jobs
	pool.set_max_threads(3);
	lt::aux::disk_job jobs[4];
	{
		std::unique_lock<std::mutex> l(g_job_mutex);
		for (auto& j : jobs)
			pool.push_back(&j);
		pool.submit_jobs();
	}
	TEST_EQUAL(pool.num_threads(), 3);
	// now check that the number of threads is reduced when the max threads is reduced
	pool.set_max_threads(2);
	std::this_thread::sleep_for(20ms);
	int const num_threads = pool.num_threads();
	TEST_EQUAL(num_threads, 2);
}
