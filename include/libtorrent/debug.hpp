/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifndef TORRENT_DEBUG_HPP_INCLUDED
#define TORRENT_DEBUG_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

#if TORRENT_USE_ASSERTS && defined BOOST_HAS_PTHREADS
#include <pthread.h>
#endif

#if defined TORRENT_ASIO_DEBUGGING

#include "libtorrent/assert.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/time.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <map>
#include <cstring>
#include <deque>

#ifdef __MACH__
#include <mach/task_info.h>
#include <mach/task.h>
#include <mach/mach_init.h>

const mach_msg_type_number_t task_events_info_count = TASK_EVENTS_INFO_COUNT;
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

std::string demangle(char const* name);

namespace libtorrent
{
	struct async_t
	{
		async_t() : refs(0) {}
		std::string stack;
		int refs;
	};

	// defined in session_impl.cpp
	extern std::map<std::string, async_t> _async_ops;
	extern int _async_ops_nthreads;
	extern mutex _async_ops_mutex;

	// timestamp -> operation
	struct wakeup_t
	{
		time_point timestamp;
		boost::uint64_t context_switches;
		char const* operation;
	};
	extern std::deque<wakeup_t> _wakeups;

	inline bool has_outstanding_async(char const* name)
	{
		mutex::scoped_lock l(_async_ops_mutex);
		std::map<std::string, async_t>::iterator i = _async_ops.find(name);
		return i != _async_ops.end();
	}

	inline void add_outstanding_async(char const* name)
	{
		mutex::scoped_lock l(_async_ops_mutex);
		async_t& a = _async_ops[name];
		if (a.stack.empty())
		{
			char stack_text[10000];
			print_backtrace(stack_text, sizeof(stack_text), 9);

			// skip the stack frame of 'add_outstanding_async'
			char* ptr = strchr(stack_text, '\n');
			if (ptr != NULL) ++ptr;
			else ptr = stack_text;
			a.stack = ptr;
		}
		++a.refs;
	}

	inline void complete_async(char const* name)
	{
		mutex::scoped_lock l(_async_ops_mutex);
		async_t& a = _async_ops[name];
		TORRENT_ASSERT(a.refs > 0);
		--a.refs;

		// don't let this grow indefinitely
		if (_wakeups.size() < 100000)
		{
			_wakeups.push_back(wakeup_t());
			wakeup_t& w = _wakeups.back();
			w.timestamp = clock_type::now();
#ifdef __MACH__
			task_events_info teinfo;
			mach_msg_type_number_t t_info_count = task_events_info_count;
			task_info(mach_task_self(), TASK_EVENTS_INFO,
				reinterpret_cast<task_info_t>(&teinfo), &t_info_count);
			w.context_switches = teinfo.csw;
#else
			w.context_switches = 0;
#endif
			w.operation = name;
		}
	}

	inline void async_inc_threads()
	{
		mutex::scoped_lock l(_async_ops_mutex);
		++_async_ops_nthreads;
	}

	inline void async_dec_threads()
	{
		mutex::scoped_lock l(_async_ops_mutex);
		--_async_ops_nthreads;
	}

	inline int log_async()
	{
		mutex::scoped_lock l(_async_ops_mutex);
		int ret = 0;
		for (std::map<std::string, async_t>::iterator i = _async_ops.begin()
			, end(_async_ops.end()); i != end; ++i)
		{
			if (i->second.refs <= _async_ops_nthreads - 1) continue;
			ret += i->second.refs;
			printf("%s: (%d)\n%s\n", i->first.c_str(), i->second.refs, i->second.stack.c_str());
		}
		return ret;
	}
}

#endif // TORRENT_ASIO_DEBUGGING

namespace libtorrent
{
#if TORRENT_USE_ASSERTS && defined BOOST_HAS_PTHREADS
	struct single_threaded
	{
		single_threaded(): m_single_thread(0) {}
		~single_threaded() { m_single_thread = 0; }
		bool is_single_thread() const
		{
			if (m_single_thread == 0)
			{
				m_single_thread = pthread_self();
				return true;
			}
			return m_single_thread == pthread_self();
		}
		bool is_not_thread() const
		{
			if (m_single_thread == 0) return true;
			return m_single_thread != pthread_self();
		}

		void thread_started()
		{ m_single_thread = pthread_self(); }

		void transfer_ownership()
		{ m_single_thread = 0; }

	private:
		mutable pthread_t m_single_thread;
	};
#else
	struct single_threaded {
		bool is_single_thread() const { return true; }
		void thread_started() {}
		bool is_not_thread() const {return true; }
	};
#endif

#if TORRENT_USE_ASSERTS
	struct increment_guard
	{
		int& m_cnt;
		increment_guard(int& c) : m_cnt(c) { TORRENT_ASSERT(m_cnt >= 0); ++m_cnt; }
		~increment_guard() { --m_cnt; TORRENT_ASSERT(m_cnt >= 0); }
	private:
		increment_guard(increment_guard const&);
		increment_guard operator=(increment_guard const&);
	};
#define TORRENT_INCREMENT(x) increment_guard inc_(x)
#else
#define TORRENT_INCREMENT(x) do {} while (false)
#endif
}

#endif // TORRENT_DEBUG_HPP_INCLUDED

