/*

Copyright (c) 2010-2018, Arvid Norberg
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

#include "libtorrent/thread.hpp"
#include "libtorrent/assert.hpp"

#ifdef TORRENT_BEOS
#include <kernel/OS.h>
#endif

#ifdef BOOST_HAS_PTHREADS
#include <sys/time.h> // for gettimeofday()
#include <boost/cstdint.hpp>
#endif

#include <algorithm>

namespace libtorrent
{

	void sleep(int milliseconds)
	{
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
		Sleep(milliseconds);
#elif defined TORRENT_BEOS
		snooze_until(system_time() + boost::int64_t(milliseconds) * 1000, B_SYSTEM_TIMEBASE);
#else
		usleep(milliseconds * 1000);
#endif
	}

#ifdef BOOST_HAS_PTHREADS
	recursive_mutex::recursive_mutex()
	{
		::pthread_mutexattr_t attr;
		::pthread_mutexattr_init(&attr);
		::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		::pthread_mutex_init(&m_mutex, &attr);
		::pthread_mutexattr_destroy(&attr);
	}

	recursive_mutex::~recursive_mutex()
	{ ::pthread_mutex_destroy(&m_mutex); }

	void recursive_mutex::lock()
	{ ::pthread_mutex_lock(&m_mutex); }

	void recursive_mutex::unlock()
	{ ::pthread_mutex_unlock(&m_mutex); }
#else

namespace {
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	thread_id_t thread_self()
	{
		return GetCurrentThreadId();
	}
#elif defined TORRENT_BEOS
	thread_id_t thread_self()
	{
		return find_thread(NULL);
	}
#endif
}

	recursive_mutex::recursive_mutex()
		: m_owner()
		, m_count(0)
	{}

	recursive_mutex::~recursive_mutex() {}

	void recursive_mutex::lock()
	{
		thread_id_t const self = thread_self();

		mutex::scoped_lock lock(m_mutex);
		if (m_owner == self)
		{
			++m_count;
		}
		else if (m_count == 0)
		{
			TORRENT_ASSERT(m_owner == thread_id_t());
			m_owner = self;
			m_count = 1;
		}
		else
		{
			while (m_count != 0)
				m_cond.wait(lock);
			TORRENT_ASSERT(m_count == 0);
			TORRENT_ASSERT(m_owner == thread_id_t());
			m_owner = self;
			m_count = 1;
		}
	}

	void recursive_mutex::unlock()
	{
		thread_id_t const self = thread_self();
		mutex::scoped_lock lock(m_mutex);
		TORRENT_ASSERT(m_owner == self);
		TORRENT_ASSERT(m_count > 0);

		if (--m_count == 0)
		{
			m_owner = thread_id_t();
			m_cond.notify();
		}
	}
#endif

#ifdef BOOST_HAS_PTHREADS
	condition_variable::condition_variable()
	{
		pthread_cond_init(&m_cond, 0);
	}

	condition_variable::~condition_variable()
	{
		pthread_cond_destroy(&m_cond);
	}

	template <typename LockGuard>
	void condition_variable::wait_impl(LockGuard& l)
	{
		TORRENT_ASSERT(l.locked());
		// wow, this is quite a hack
		pthread_cond_wait(&m_cond, reinterpret_cast<pthread_mutex_t*>(&l.mutex()));
	}

	template <typename LockGuard>
	void condition_variable::wait_for_impl(LockGuard& l, time_duration rel_time)
	{
		TORRENT_ASSERT(l.locked());

		struct timeval tv;
		struct timespec ts;
		gettimeofday(&tv, NULL);
		boost::uint64_t microseconds = tv.tv_usec + total_microseconds(rel_time) % 1000000;
		ts.tv_nsec = (microseconds % 1000000) * 1000;
		ts.tv_sec = tv.tv_sec + total_seconds(rel_time) + microseconds / 1000000;

		// wow, this is quite a hack
		pthread_cond_timedwait(&m_cond, reinterpret_cast<pthread_mutex_t*>(&l.mutex()), &ts);
	}

	void condition_variable::notify_all()
	{
		pthread_cond_broadcast(&m_cond);
	}

	void condition_variable::notify()
	{
		pthread_cond_signal(&m_cond);
	}
#elif defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	condition_variable::condition_variable()
		: m_num_waiters(0)
	{
#if _WIN32_WINNT <= 0x0501
		m_sem = CreateSemaphore(0, 0, INT_MAX, 0);
#else
		m_sem = CreateSemaphoreEx(0, 0, INT_MAX, 0, 0, SEMAPHORE_ALL_ACCESS);
#endif
	}

	condition_variable::~condition_variable()
	{
		CloseHandle(m_sem);
	}

	template <typename LockGuard>
	void condition_variable::wait_impl(LockGuard& l)
	{
		TORRENT_ASSERT(l.locked());
		++m_num_waiters;
		l.unlock();
		WaitForSingleObjectEx(m_sem, INFINITE, FALSE);
		l.lock();
		--m_num_waiters;
	}

	template <typename LockGuard>
	void condition_variable::wait_for_impl(LockGuard& l, time_duration rel_time)
	{
		TORRENT_ASSERT(l.locked());
		++m_num_waiters;
		l.unlock();
		WaitForSingleObjectEx(m_sem, total_milliseconds(rel_time), FALSE);
		l.lock();
		--m_num_waiters;
	}

	void condition_variable::notify_all()
	{
		ReleaseSemaphore(m_sem, m_num_waiters, 0);
	}

	void condition_variable::notify()
	{
		ReleaseSemaphore(m_sem, (std::min)(m_num_waiters, 1), 0);
	}
#elif defined TORRENT_BEOS
	condition_variable::condition_variable()
		: m_num_waiters(0)
	{
		m_sem = create_sem(0, 0);
	}

	condition_variable::~condition_variable()
	{
		delete_sem(m_sem);
	}

	template <typename LockGuard>
	void condition_variable::wait_impl(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		++m_num_waiters;
		l.unlock();
		acquire_sem(m_sem);
		l.lock();
		--m_num_waiters;
	}

	template <typename LockGuard>
	void condition_variable::wait_for_impl(LockGuard& l, time_duration rel_time)
	{
		TORRENT_ASSERT(l.locked());
		++m_num_waiters;
		l.unlock();
		acquire_sem_etc(m_sem, 1, B_RELATIVE_TIMEOUT, total_microseconds(rel_time));
		l.lock();
		--m_num_waiters;
	}

	void condition_variable::notify_all()
	{
		release_sem_etc(m_sem, m_num_waiters, 0);
	}

	void condition_variable::notify()
	{
		release_sem_etc(m_sem, (std::min)(m_num_waiters, 1), 0);
	}
#else
#error not implemented
#endif

	void condition_variable::wait(mutex::scoped_lock& l)
	{ wait_impl(l); }
	void condition_variable::wait_for(mutex::scoped_lock& l, time_duration rel_time)
	{ wait_for_impl(l, rel_time); }
	void condition_variable::wait(recursive_mutex::scoped_lock& l)
	{ wait_impl(l); }
	void condition_variable::wait_for(recursive_mutex::scoped_lock& l, time_duration rel_time)
	{ wait_for_impl(l, rel_time); }

	// explicitly instantiate for these locks
	template void condition_variable::wait_impl<mutex::scoped_lock>(mutex::scoped_lock&);
	template void condition_variable::wait_impl<recursive_mutex::scoped_lock>(recursive_mutex::scoped_lock&);
	template void condition_variable::wait_for_impl<mutex::scoped_lock>(mutex::scoped_lock&, time_duration);
	template void condition_variable::wait_for_impl<recursive_mutex::scoped_lock>(recursive_mutex::scoped_lock&, time_duration);
}

