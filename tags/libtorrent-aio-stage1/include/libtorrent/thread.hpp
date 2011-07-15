/*

Copyright (c) 2009, Arvid Norberg
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

#ifndef TORRENT_THREAD_HPP_INCLUDED
#define TORRENT_THREAD_HPP_INCLUDED

#include "libtorrent/config.hpp"

#include <memory>

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#include <memory> // for auto_ptr required by asio

#include <boost/asio/detail/thread.hpp>
#include <boost/asio/detail/mutex.hpp>
#include <boost/asio/detail/event.hpp>

#if TORRENT_USE_POSIX_SEMAPHORE
#include <semaphore.h>      // sem_*
#endif

#if TORRENT_USE_MACH_SEMAPHORE
#include <mach/semaphore.h> // semaphore_signal, semaphore_wait
#include <mach/task.h>      // semaphore_create, semaphore_destroy
#include <mach/mach_init.h> // current_task
#endif

namespace libtorrent
{
	typedef boost::asio::detail::thread thread;
	typedef boost::asio::detail::mutex mutex;
	typedef boost::asio::detail::event event;

	TORRENT_EXPORT void sleep(int milliseconds);

	struct TORRENT_EXPORT condition
	{
		condition();
		~condition();
		void wait(mutex::scoped_lock& l);
		void signal_all(mutex::scoped_lock& l);
	private:
#ifdef BOOST_HAS_PTHREADS
		pthread_cond_t m_cond;
#elif defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
		HANDLE m_sem;
		mutex m_mutex;
		int m_num_waiters;
#else
#error not implemented
#endif
	};

	// #error these semaphores needs to release all threads that are waiting for the semaphore when signalled
#if TORRENT_USE_POSIX_SEMAPHORE
	struct TORRENT_EXPORT semaphore
	{
		semaphore() { sem_init(&m_sem, 0, 0); }
		~semaphore() { sem_destroy(&m_sem); }
		void signal() { sem_post(&m_sem); }
		void signal_all()
		{
			int waiters = 0;
			do
			{
				// when anyone is waiting, waiters will be
				// 0 or negative. 0 means one might be waiting
				// -1 means 2 are waiting. Keep posting as long
				// we see negative values or 0
				sem_getvalue(&m_sem, &waiters);
				sem_post(&m_sem);
			} while (waiters < 0)
		}
		void wait() { sem_wait(&m_sem); }
		void timed_wait(int ms)
		{
			timespec sp = { ms / 1000, (ms % 1000) * 1000000 };
			sem_timedwait(&m_sem, &sp);
		}
		sem_t m_sem;
	};
#elif TORRENT_USE_MACH_SEMAPHORE
	struct TORRENT_EXPORT semaphore
	{
		semaphore() { semaphore_create(current_task(), &m_sem, SYNC_POLICY_FIFO, 0); }
		~semaphore() { semaphore_destroy(current_task(), m_sem); }
		void signal() { semaphore_signal(m_sem); }
		void signal_all() { semaphore_signal_all(m_sem); }
		void wait() { semaphore_wait(m_sem); }
		void timed_wait(int ms)
		{
			mach_timespec_t sp = { ms / 1000, (ms % 1000) * 100000};
			semaphore_timedwait(m_sem, sp);
		}
		semaphore_t m_sem;
	};
#elif defined TORRENT_WINDOWS
	struct TORRENT_EXPORT semaphore
	{
		semaphore() { m_sem = CreateSemaphore(0, 0, 100, 0); }
		~semaphore() { CloseHandle(m_sem); }
		void signal() { ReleaseSemaphore(m_sem, 1, 0); }
		void signal_all()
		{
			LONG prev = 0;
			do { ReleaseSemaphore(m_sem, 1, &prev); } while (prev > 1);
		}
		void wait() { WaitForSingleObject(m_sem, INFINITE); }
		void timed_wait(int ms) { WaitForSingleObject(m_sem, ms); }
		HANDLE m_sem;
	};
#endif
}

#endif

