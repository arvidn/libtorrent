/*

Copyright (c) 2016, Steven Siloti
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
#include "simulator/simulator.hpp"
#include "libtorrent/disk_io_thread_pool.hpp"
#include <condition_variable>


struct test_threads : lt::pool_thread_interface
{
	test_threads() {}

	void notify_all() override { m_cond.notify_all(); }
	void thread_fun(lt::disk_io_thread_pool&, lt::io_service::work) override
	{
		std::unique_lock<std::mutex> l(m_mutex);
		for (;;)
		{
			m_pool->thread_idle();
			while (!m_pool->should_exit() && m_active_threads >= m_target_active_threads)
				m_cond.wait(l);
			m_pool->thread_active();

			if (m_pool->try_thread_exit(std::this_thread::get_id()))
				break;

			if (m_active_threads < m_target_active_threads)
			{
				++m_active_threads;
				while (!m_pool->should_exit() && m_active_threads <= m_target_active_threads)
					m_cond.wait(l);
				--m_active_threads;
			}

			if (m_pool->try_thread_exit(std::this_thread::get_id()))
				break;
		}

		l.unlock();
		m_exit_cond.notify_all();
	}

	// change the number of active threads and wait for the threads
	// to settle at the new value
	void set_active_threads(int target)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		assert(target <= m_pool->num_threads());
		m_target_active_threads = target;
		while (m_active_threads != m_target_active_threads)
		{
			l.unlock();
			m_cond.notify_all();
			std::this_thread::yield();
			l.lock();
		}
	}

	// this is to close a race between a thread exiting and a test checking the
	// thread count
	void wait_for_thread_exit(int num_threads)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		m_exit_cond.wait_for(l, std::chrono::seconds(30), [&]()
		{
			return m_pool->num_threads() == num_threads;
		});
	}

	lt::disk_io_thread_pool* m_pool;
	std::mutex m_mutex;
	std::condition_variable m_cond;
	std::condition_variable m_exit_cond;

	// must hold m_mutex to access
	int m_active_threads = 0;
	// must hold m_mutex to access
	int m_target_active_threads = 0;
};

/*
TORRENT_TEST(disk_io_thread_pool_idle_reaping)
{
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	test_threads threads;
	sim::asio::io_service ios(sim);
	lt::disk_io_thread_pool pool(threads, ios);
	threads.m_pool = &pool;
	pool.set_max_threads(3);
	pool.job_queued(3);
	TEST_EQUAL(pool.num_threads(), 3);
	// make sure all the threads are up and settled in the active state
	threads.set_active_threads(3);

	// first just kill one thread
	threads.set_active_threads(2);
	lt::deadline_timer idle_delay(ios);
	// the thread will be killed the second time the reaper runs and we need
	// to wait one extra minute to make sure the check runs after the reaper
	idle_delay.expires_from_now(std::chrono::minutes(3));
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
	sim.reset();

	// now kill the rest
	threads.set_active_threads(0);
	idle_delay.expires_from_now(std::chrono::minutes(3));
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

	test_threads threads;
	sim::asio::io_service ios(sim);
	lt::disk_io_thread_pool pool(threads, ios);
	threads.m_pool = &pool;
	pool.set_max_threads(3);
	pool.job_queued(3);
	TEST_EQUAL(pool.num_threads(), 3);
	pool.abort(true);
	TEST_EQUAL(pool.num_threads(), 0);
}

#if 0
// disabled for now because io_service::work doesn't work under the simulator
// and we need it to stop this test from exiting prematurely
TORRENT_TEST(disk_io_thread_pool_abort_no_wait)
{
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	test_threads threads;
	sim::asio::io_service ios(sim);
	lt::disk_io_thread_pool pool(threads, ios);
	threads.m_pool = &pool;
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

	test_threads threads;
	sim::asio::io_service ios(sim);
	lt::disk_io_thread_pool pool(threads, ios);
	threads.m_pool = &pool;
	// first check that the thread limit is respected when adding jobs
	pool.set_max_threads(3);
	pool.job_queued(4);
	TEST_EQUAL(pool.num_threads(), 3);
	// now check that the number of threads is reduced when the max threads is reduced
	pool.set_max_threads(2);
	// see comment above about this kludge
	threads.wait_for_thread_exit(2);
	TEST_EQUAL(pool.num_threads(), 2);
}
