/*

Copyright (c) 2010, Arvid Norberg
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

	condition::condition()
	{
		pthread_cond_init(&m_cond, 0);
	}

	condition::~condition()
	{
		pthread_cond_destroy(&m_cond);
	}

	void condition::wait(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		// wow, this is quite a hack
		pthread_cond_wait(&m_cond, (::pthread_mutex_t*)&l.mutex());
	}

	void condition::signal_all(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		pthread_cond_broadcast(&m_cond);
	}
#elif defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	condition::condition()
		: m_num_waiters(0)
	{
		m_sem = CreateSemaphore(0, 0, INT_MAX, 0);
	}

	condition::~condition()
	{
		CloseHandle(m_sem);
	}

	void condition::wait(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		++m_num_waiters;
		l.unlock();
		WaitForSingleObject(m_sem, INFINITE);
		l.lock();
		--m_num_waiters;
	}

	void condition::signal_all(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		ReleaseSemaphore(m_sem, m_num_waiters, 0);
	}
#elif defined TORRENT_BEOS
	condition::condition()
		: m_num_waiters(0)
	{
		m_sem = create_sem(0, 0);
	}

	condition::~condition()
	{
		delete_sem(m_sem);
	}

	void condition::wait(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		++m_num_waiters;
		l.unlock();
		acquire_sem(m_sem);
		l.lock();
		--m_num_waiters;
	}

	void condition::signal_all(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		release_sem_etc(m_sem, m_num_waiters, 0);
	}
#else
#error not implemented
#endif

}

