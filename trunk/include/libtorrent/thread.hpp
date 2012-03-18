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

#if defined TORRENT_BEOS
#include <kernel/OS.h>
#endif

#include <memory> // for auto_ptr required by asio

#include <boost/asio/detail/thread.hpp>
#include <boost/asio/detail/mutex.hpp>
#include <boost/asio/detail/event.hpp>

namespace libtorrent
{
	typedef boost::asio::detail::thread thread;
	typedef boost::asio::detail::mutex mutex;
	typedef boost::asio::detail::event event;

	TORRENT_EXPORT void sleep(int milliseconds);

	struct TORRENT_EXTRA_EXPORT condition
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
#elif defined TORRENT_BEOS
		sem_id m_sem;
		mutex m_mutex;
		int m_num_waiters;
#else
#error not implemented
#endif
	};
}

#endif

