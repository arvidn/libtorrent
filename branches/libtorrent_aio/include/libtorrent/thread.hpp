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

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#include <boost/asio/detail/thread.hpp>
#include <boost/asio/detail/mutex.hpp>
#include <boost/asio/detail/event.hpp>

#ifdef TORRENT_BEOS
#include <kernel/OS.h>
#endif

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
	typedef boost::asio::detail::event condition;

	inline void sleep(int milliseconds)
	{
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
		Sleep(milliseconds);
#elif defined TORRENT_BEOS
		snooze_until(system_time() + boost::int64_t(milliseconds) * 1000, B_SYSTEM_TIMEBASE);
#else
		usleep(milliseconds * 1000);
#endif
	}

	// #error these semaphores needs to release all threads that are waiting for the semaphore when signalled
#if TORRENT_USE_POSIX_SEMAPHORE
	struct semaphore
	{
		semaphore() { sem_init(&m_sem, 0, 0); }
		~semaphore() { sem_destroy(&m_sem); }
		void signal() { sem_post(&m_sem); }
		void wait() { sem_wait(&m_sem); }
		sem_t m_sem;
	};
#elif TORRENT_USE_MACH_SEMAPHORE
	struct semaphore
	{
		semaphore() { semaphore_create(current_task(), &m_sem, SYNC_POLICY_FIFO, 0); }
		~semaphore() { semaphore_destroy(current_task(), m_sem); }
		void signal() { semaphore_signal(m_sem); }
		void wait() { semaphore_wait(m_sem); }
		semaphore_t m_sem;
	};
#elif defined TORRENT_WINDOWS
	struct semaphore
	{
		semaphore() { m_sem = CreateSemaphore(0, 0, 100, 0); }
		~semaphore() { CloseHandle(m_sem); }
		void signal() { ReleaseSemaphore(m_sem, 1, 0); }
		void wait() { WaitForSingleObject(m_sem, INFINITE); }
		HANDLE m_sem;
	};
#endif
}

#endif

