/*

Copyright (c) 2005-2016, Arvid Norberg, Steven Siloti
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

#ifndef TORRENT_DISK_IO_THREAD_POOL
#define TORRENT_DISK_IO_THREAD_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/error_code.hpp"

#include <thread>
#include <mutex>
#include <atomic>

namespace libtorrent {

	struct disk_io_thread_pool;

	struct pool_thread_interface
	{
		virtual ~pool_thread_interface() {}

		virtual void notify_all() = 0;
		virtual void thread_fun(disk_io_thread_pool&, io_service::work) = 0;
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
			, io_service& ios);
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
	};
} // namespace libtorrent

#endif
