/*

Copyright (c) 2016, Steven Siloti
Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2017-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_IO_THREAD_POOL
#define TORRENT_DISK_IO_THREAD_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/error_code.hpp"

#include <thread>
#include <mutex>
#include <atomic>

namespace lt::aux {

	struct disk_io_thread_pool;

	struct pool_thread_interface
	{
		virtual ~pool_thread_interface() {}

		virtual void notify_all() = 0;
		virtual void thread_fun(disk_io_thread_pool&, executor_work_guard<io_context::executor_type>) = 0;
	};

	// this class implements the policy for creating and destroying I/O threads
	// threads are created when job_queued is called to signal the arrival of
	// new jobs
	// once a minute threads are destroyed if at least one thread has been
	// idle for the entire minute
	// the pool_thread_interface is used to spawn and notify the worker threads
	struct TORRENT_EXTRA_EXPORT disk_io_thread_pool
	{
		disk_io_thread_pool(pool_thread_interface& thread_iface
			, io_context& ios);
		~disk_io_thread_pool();

		// set the maximum number of I/O threads which may be running
		// the actual number of threads will be <= this number
		void set_max_threads(int i);
		void abort(bool wait);
		int max_threads() const { return m_max_threads; }

		// thread_idle, thread_active, and job_queued are NOT thread safe
		// all calls to them must be serialized
		// it is expected that they will be called while holding the
		// job queue mutex

		// these functions should be called by the thread_fun to signal its state
		// threads are considered active when they are started so thread_idle should
		// be called first
		// these calls are not thread safe
		void thread_idle() { ++m_num_idle_threads; }
		void thread_active();

		// check if there is an outstanding request for I/O threads to stop
		// this is a weak check, if it returns true try_thread_exit may still
		// return false
		bool should_exit() { return m_threads_to_exit > 0; }
		// this should be the last function an I/O thread calls before breaking
		// out of its service loop
		// if it returns true then the thread MUST exit
		// if it returns false the thread should not exit
		bool try_thread_exit(std::thread::id id);

		// get the thread id of the first thread in the internal vector
		// since this is the first thread it will remain the same until the first
		// thread exits
		// it can be used to trigger maintenance jobs which should only run on one thread
		std::thread::id first_thread_id();
		int num_threads()
		{
			std::lock_guard<std::mutex> l(m_mutex);
			return int(m_threads.size());
		}

		// this should be called whenever new jobs are queued
		// queue_size is the current size of the job queue
		// not thread safe
		void job_queued(int queue_size);

	private:
		void reap_idle_threads(error_code const& ec);

		// the caller must hold m_mutex
		void stop_threads(int num_to_stop);

		pool_thread_interface& m_thread_iface;

		std::atomic<int> m_max_threads;
		// the number of threads the reaper decided should exit
		std::atomic<int> m_threads_to_exit;

		// must hold m_mutex to access
		bool m_abort;

		std::atomic<int> m_num_idle_threads;
		// the minimum number of idle threads seen since the last reaping
		std::atomic<int> m_min_idle_threads;

		// ensures thread creation/destruction is atomic
		std::mutex m_mutex;

		// the actual threads running disk jobs
		std::vector<std::thread> m_threads;

		// timer to check for and reap idle threads
		deadline_timer m_idle_timer;

		io_context& m_ioc;
	};
} // namespace lt::aux

#endif
