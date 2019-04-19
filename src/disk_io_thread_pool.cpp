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

#include "libtorrent/disk_io_thread_pool.hpp"
#include "libtorrent/assert.hpp"

#include <algorithm>

namespace {

	constexpr std::chrono::seconds reap_idle_threads_interval(60);
}

namespace libtorrent {

	disk_io_thread_pool::disk_io_thread_pool(pool_thread_interface& thread_iface
		, io_service& ios)
		: m_thread_iface(thread_iface)
		, m_max_threads(0)
		, m_threads_to_exit(0)
		, m_abort(false)
		, m_num_idle_threads(0)
		, m_min_idle_threads(0)
		, m_idle_timer(ios)
	{}

	disk_io_thread_pool::~disk_io_thread_pool()
	{
		abort(true);
	}

	void disk_io_thread_pool::set_max_threads(int const i)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		if (i == m_max_threads) return;
		m_max_threads = i;
		if (int(m_threads.size()) < i) return;
		stop_threads(int(m_threads.size()) - i);
	}

	void disk_io_thread_pool::abort(bool wait)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		if (m_abort) return;
		m_abort = true;
		m_idle_timer.cancel();
		stop_threads(int(m_threads.size()));
		for (auto& t : m_threads)
		{
			if (wait)
			{
				// must release m_mutex to avoid a deadlock if the thread
				// tries to acquire it
				l.unlock();
				t.join();
				l.lock();
			}
			else
				t.detach();
		}
		m_threads.clear();
	}

	void disk_io_thread_pool::thread_active()
	{
		int const num_idle_threads = --m_num_idle_threads;
		TORRENT_ASSERT(num_idle_threads >= 0);

		int current_min = m_min_idle_threads;
		while (num_idle_threads < current_min
			&& !m_min_idle_threads.compare_exchange_weak(current_min, num_idle_threads));
	}

	bool disk_io_thread_pool::try_thread_exit(std::thread::id id)
	{
		int to_exit = m_threads_to_exit;
		while (to_exit > 0 &&
			!m_threads_to_exit.compare_exchange_weak(to_exit, to_exit - 1));
		if (to_exit > 0)
		{
			std::unique_lock<std::mutex> l(m_mutex);
			if (!m_abort)
			{
				auto new_end = std::remove_if(m_threads.begin(), m_threads.end()
					, [id](std::thread& t)
				{
					if (t.get_id() == id)
					{
						t.detach();
						return true;
					}
					return false;
				});
				TORRENT_ASSERT(new_end != m_threads.end());
				m_threads.erase(new_end, m_threads.end());
				if (m_threads.empty()) m_idle_timer.cancel();
			}
		}
		return to_exit > 0;
	}

	std::thread::id disk_io_thread_pool::first_thread_id()
	{
		std::lock_guard<std::mutex> l(m_mutex);
		if (m_threads.empty()) return {};
		return m_threads.front().get_id();
	}

	void disk_io_thread_pool::job_queued(int const queue_size)
	{
		// this check is not strictly necessary
		// but do it to avoid acquiring the mutex in the trivial case
		if (m_num_idle_threads >= queue_size) return;
		std::lock_guard<std::mutex> l(m_mutex);
		if (m_abort) return;

		// reduce the number of threads requested to stop if we're going to need
		// them for these new jobs
		int to_exit = m_threads_to_exit;
		while (to_exit > std::max(0, m_num_idle_threads - queue_size) &&
			!m_threads_to_exit.compare_exchange_weak(to_exit
				, std::max(0, m_num_idle_threads - queue_size)));

		// now start threads until we either have enough to service
		// all queued jobs without blocking or hit the max
		for (int i = m_num_idle_threads
			; i < queue_size && int(m_threads.size()) < m_max_threads
			; ++i)
		{
			// if this is the first thread started, start the reaper timer
			if (m_threads.empty())
			{
				m_idle_timer.expires_from_now(reap_idle_threads_interval);
				m_idle_timer.async_wait([this](error_code const& ec) { reap_idle_threads(ec); });
			}

			// work keeps the io_service::run() call blocked from returning.
			// When shutting down, it's possible that the event queue is drained
			// before the disk_io_thread has posted its last callback. When this
			// happens, the io_service will have a pending callback from the
			// disk_io_thread, but the event loop is not running. this means
			// that the event is destructed after the disk_io_thread. If the
			// event refers to a disk buffer it will try to free it, but the
			// buffer pool won't exist anymore, and crash. This prevents that.
			m_threads.emplace_back(&pool_thread_interface::thread_fun
				, &m_thread_iface, std::ref(*this)
				, io_service::work(get_io_service(m_idle_timer)));
		}
	}

	void disk_io_thread_pool::reap_idle_threads(error_code const& ec)
	{
		// take the minimum number of idle threads during the last
		// sample period and request that many threads to exit
		if (ec) return;
		std::lock_guard<std::mutex> l(m_mutex);
		if (m_abort) return;
		if (m_threads.empty()) return;
		m_idle_timer.expires_from_now(reap_idle_threads_interval);
		m_idle_timer.async_wait([this](error_code const& e) { reap_idle_threads(e); });
		int const min_idle = m_min_idle_threads.exchange(m_num_idle_threads);
		if (min_idle <= 0) return;
		// stop either the minimum number of idle threads or the number of threads
		// which must be stopped to get below the max, whichever is larger
		int const to_stop = std::max(min_idle, int(m_threads.size()) - m_max_threads);
		stop_threads(to_stop);
	}

	void disk_io_thread_pool::stop_threads(int num_to_stop)
	{
		m_threads_to_exit = num_to_stop;
		m_thread_iface.notify_all();
	}

} // namespace libtorrent
