/*

Copyright (c) 2011-2013, Arvid Norberg
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

#ifndef TORRENT_THREAD_POOL
#define TORRENT_THREAD_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/thread.hpp"
#include <boost/atomic.hpp>
#include <deque>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

namespace libtorrent
{

	template <class T>
	struct thread_pool
	{
		thread_pool() : m_num_threads(0) {}
		virtual ~thread_pool() {}
		void stop() { set_num_threads(0, true); }
		void set_num_threads(int i, bool wait = true)
		{
			if (i == m_num_threads) return;
   
			if (i > m_num_threads)
			{
				while (m_num_threads < i)
				{
					++m_num_threads;
					m_threads.push_back(boost::shared_ptr<thread>(
						new thread(boost::bind(&thread_pool::thread_fun, this, int(m_num_threads)-1))));
				}
			}
			else
			{
				while (m_num_threads > i) { --m_num_threads; }
				mutex::scoped_lock l(m_mutex);
				m_cond.notify_all();
				l.unlock();
				if (wait) for (int i = m_num_threads; i < int(m_threads.size()); ++i) m_threads[i]->join();
				// this will detach the threads
				m_threads.resize(m_num_threads);
			}
		}

		// returns true if the job was posted, and false if it was
		// processed immediately
		bool post_job(T& e)
		{
			if (m_num_threads == 0)
			{
				// if we don't have any worker threads
				// just do the work immediately
				process_job(e, false);
				return false;
			}
			else
			{
				retain_job(e);
				mutex::scoped_lock l(m_mutex);
				m_queue.push_back(e);
				// we only need to signal if the threads
				// may have been put to sleep. If the size
				// previous to adding the new job was > 0
				// they don't need waking up.
				if (m_queue.size() == 1)
					m_cond.notify();
				return true;
			}
		}

	protected:

		virtual void process_job(T const& j, bool post) = 0;
		virtual void retain_job(T&) {}

	private:

		void thread_fun(int thread_id)
		{
			for (;;)
			{
				mutex::scoped_lock l(m_mutex);
				while (m_queue.empty() && thread_id < m_num_threads) m_cond.wait(l);

				// if the number of wanted thread is decreased,
				// we may stop this thread
				// when we're terminating the last hasher thread (id=0), make sure
				// we finish up all queud jobs first
				if ((thread_id != 0 || m_queue.empty()) && thread_id >= m_num_threads) break;

				TORRENT_ASSERT(!m_queue.empty());
				T e = m_queue.front();
				m_queue.pop_front();
				l.unlock();

				process_job(e, true);
			}

#ifdef TORRENT_DEBUG
			if (thread_id == 0)
			{
				// when we're terminating the last hasher thread, make sure
				// there are no more scheduled jobs
				mutex::scoped_lock l(m_mutex);
				TORRENT_ASSERT(m_queue.empty());
			}
#endif
		}

		// the mutex only protects m_cond and m_queue
		// all other members are only used from a single
		// thread (the user of this class, i.e. the disk
		// thread).
		mutex m_mutex;
		condition_variable m_cond;
		std::deque<T> m_queue;

		std::vector<boost::shared_ptr<thread> > m_threads;
		// this is a counter which is atomically incremented
		// by each thread as it's started up, in order to
		// assign a unique id to each thread
		boost::atomic<int> m_num_threads;
	};

}

#endif // TORRENT_THREAD_POOL

