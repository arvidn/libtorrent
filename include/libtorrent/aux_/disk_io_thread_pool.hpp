/*

Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2016, Steven Siloti
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

#include "libtorrent/aux_/disk_job.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/error_code.hpp"

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace libtorrent {
	// TODO: move into aux namespace
	using jobqueue_t = aux::tailqueue<aux::disk_job>;
}

namespace libtorrent::aux {

	struct disk_io_thread_pool;

	using disk_thread_fun = std::function<void(
		aux::disk_io_thread_pool&, executor_work_guard<io_context::executor_type> work)>;

	enum class wait_result: std::uint8_t
	{
		new_job,
		exit_thread,
		interrupt,
	};

	// this class implements the policy for creating and destroying I/O threads
	// threads are created when job_queued is called to signal the arrival of
	// new jobs
	// once a minute threads are destroyed if at least one thread has been
	// idle for the entire minute
	// the pool_thread_interface is used to spawn and notify the worker threads
	struct TORRENT_EXTRA_EXPORT disk_io_thread_pool
	{
		disk_io_thread_pool(disk_thread_fun thread_fun, io_context& ios);
		~disk_io_thread_pool();

		// set the maximum number of I/O threads which may be running
		// the actual number of threads will be <= this number
		void set_max_threads(int i);
		void abort(bool wait);
		int max_threads() const { return m_max_threads; }

		void notify_all()
		{
			m_job_cond.notify_all();
		}

		// returns wait_result::exit_thread if the thread should exit
		wait_result wait_for_job(std::unique_lock<std::mutex>& l);

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

		// TODO: the job mutex must be held when this is called
		void append(jobqueue_t jobs)
		{
			m_queued_jobs.append(std::move(jobs));
		}

		// TODO: the job mutex must be held when this is called
		void push_back(aux::disk_job* j)
		{
			m_queued_jobs.push_back(j);
		}

		// TODO: the job mutex must be held when this is called
		aux::disk_job* pop_front()
		{
			return m_queued_jobs.pop_front();
		}

		// TODO: the job mutex must be held when this is called
		bool empty() const
		{
			return m_queued_jobs.empty();
		}

		// TODO: the job mutex must be held when this is called
		int queue_size() const
		{
			return m_queued_jobs.size();
		}

		// TODO: the job mutex must be held when this is called
		void submit_jobs()
		{
			if (m_queued_jobs.empty()) return;
			notify_all();
			job_queued(m_queued_jobs.size());
		}

		void interrupt()
		{
			m_interrupt = true;
			m_job_cond.notify_one();
		}

		template<typename Fun>
		void visit_jobs(Fun f)
		{
			for (auto i = m_queued_jobs.iterate(); i.get(); i.next())
				f(i.get());
		}

	private:

		// this should be called whenever new jobs are queued
		// queue_size is the current size of the job queue
		// not thread safe
		void job_queued(int queue_size);

		void reap_idle_threads(error_code const& ec);

		// the caller must hold m_mutex
		void stop_threads(int num_to_stop);

		disk_thread_fun m_thread_fun;

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

		// used to wake up the disk IO thread when there are new
		// jobs on the job queue (m_queued_jobs)
		std::condition_variable m_job_cond;

		// jobs queued for servicing
		jobqueue_t m_queued_jobs;

		// when this is set, one thread is interrupted and wait_for_job() will
		// return even if the queue is empty (with the interrupt result)
		std::atomic<bool> m_interrupt;
	};
} // namespace libtorrent::aux

#endif
