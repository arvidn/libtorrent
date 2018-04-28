/*

Copyright (c) 2003-2016, Arvid Norberg, Daniel Wallin
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

#ifndef TORRENT_ALERT_MANAGER_HPP_INCLUDED
#define TORRENT_ALERT_MANAGER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/heterogeneous_queue.hpp"
#include "libtorrent/stack_allocator.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifndef TORRENT_NO_DEPRECATE
#include <boost/function/function1.hpp>
#endif
#include <boost/function/function0.hpp>
#include <boost/thread/tss.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/atomic.hpp>
#include <boost/config.hpp>
#include <list>
#include <utility> // for std::forward
#include <queue>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS
#include "libtorrent/extensions.hpp"
#endif


#ifdef __GNUC__
// this is to suppress the warnings for using std::auto_ptr
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// used for emplace_alert() variadic template emulation for c++98
#define TORRENT_ALERT_MANAGER_MAX_ARITY 7

namespace
{
	// the maximum number of cycles to a spinlock may spin
	// before yielding the cpu
	const int TORRENT_ALERT_MANAGER_SPIN_MAX = 20;
}

namespace libtorrent {

#ifndef TORRENT_DISABLE_EXTENSIONS
	struct plugin;
#endif

	class TORRENT_EXTRA_EXPORT alert_manager
	{
		// alert pool implementation
		class alert_pool
		{
		public:
			alert_pool() {}

			~alert_pool()
			{
				for (int i = 0; i < num_alert_types; i++)
				{
					mutex::scoped_lock lock(m_mutexes[i]);
					while (!m_pool[i].empty())
					{
						::free(m_pool[i].front());
						m_pool[i].pop();
					}
				}
			}

			template<typename T>
			T* acquire(T*& ptr)
			{
				TORRENT_ASSERT(T::alert_type != 0);
				mutex::scoped_lock lock(m_mutexes[T::alert_type]);
				if (m_pool[T::alert_type].empty())
				{
					ptr = (T*) ::malloc(sizeof(T));
					TORRENT_ASSERT(ptr->type() == T::alert_type);
					return ptr;
				}
				else
				{
					ptr = (T*) m_pool[T::alert_type].front();
					m_pool[T::alert_type].pop();
					return ptr;
				}
				// TODO: Construct alert here
			}

			void release(alert*const alert)
			{
				TORRENT_ASSERT(alert->type() != 0);
				mutex::scoped_lock lock(m_mutexes[alert->type()]);
				//alert->~alert();	// this assumes an alert was constructed in-place
				m_pool[alert->type()].push(alert);
			}

		private:
			mutex m_mutexes[num_alert_types];
			std::queue<alert*> m_pool[num_alert_types];
		};

		// implementation of a lock-free queue. Only push() is completely
		// thread safe. pop_all() is not mutually exclusive of push() but
		// it is not reentrant so it can only be called by one thread
		class lockfree_queue
		{
		public:
			lockfree_queue(int size_limit)
				: m_size(0)
				, m_write_slot(-1)
				, m_read_slot(0)
				, m_size_limit(size_limit)
			{
				m_storage = new boost::atomic<alert*>[size_limit * 2];
				for (int i = 0; i < m_size_limit * 2; i++)
					m_storage[i].store(NULL);
			}

			~lockfree_queue()
			{
				delete[] m_storage;
			}

			lockfree_queue& operator=(const lockfree_queue& queue)
			{
				m_size = int(queue.m_size);
				m_write_slot = int(queue.m_write_slot);
				m_read_slot = int(queue.m_read_slot);
				m_size_limit = int(queue.m_size_limit);
				m_storage = new boost::atomic<alert*>[m_size_limit * 2];

				for (int i = 0; i < m_size_limit * 2; i++)
					m_storage[i].store(NULL);

				return *this;
			}

			// get the queue size
			int size() const
			{
				return m_size;
			}

			// gets the queue size limit
			int size_limit() const
			{
				return m_size_limit;
			}

			// gets the alert at the front of the queue
			alert* front()
			{
				return m_storage[m_read_slot.load()];
			}

			// attempts to add an alert to the queue and return the
			// (virtual) index at which the alert pointer was stored.
			// If the push fails because the queue is full returns -1.
			int push(alert*const a, const int priority)
			{
				int current, next;
				alert* expected;

				TORRENT_ASSERT(a != NULL);

				const int size_limit = m_size_limit;

				// here we're reserving the next available queue slot atomically.
				// the actual write is done next on the spinlock but the alert does
				// not become visible to get_all() until we increment m_queue_size
				// TODO: Ensure fairness
				do
				{
					// get the next slot on the ring buffer
					next = (current = m_write_slot.load()) + 1;
					if (next == (size_limit * 2))
						next = 0;

					// calculate the real queue size, counting alerts that are not
					// yet included in m_queue_size because other threads are already
					// passed this check but not yet incremented m_queue_size. It is
					// important that m_queue_read_slot is loaded after m_queue_write_slot
					// or we may get the wrong result.
					const int read_slot = m_read_slot.load();
					const int real_size = (next > read_slot) ? (next - read_slot)
						: ((next < read_slot) ? ((size_limit * 2) - (read_slot - next))
						: m_size.load());

					// make sure that posting this alert will not exceed
					// the limit
					if (real_size >= size_limit * (1 + priority))
						return -1;

					TORRENT_ASSERT(next >= 0 && next < (size_limit * 2));
				}
				while (!m_write_slot.compare_exchange_strong(current, next));

				// if an alert was just popped from this slot it is possible that get_all()
				// has not yet freed the queue slot. This is extremely unlikely with a properly
				// tuned m_queue_size. If it does happens it should never put the thread off
				// cpu unless the user thread is preempted before it frees the slot. Even then
				// we're guaranteed to make progress again after only 1 scheduler cycle.
				for (int spins = 0; !m_storage[next].compare_exchange_strong((expected = NULL), a); spins++)
					if (spins >= TORRENT_ALERT_MANAGER_SPIN_MAX) boost::this_thread::yield();

				// this makes the alert visible to get_all()
				current = m_size.fetch_add(1);

				TORRENT_ASSERT(next <= (size_limit * 2));
				return current;
			}

			int pop_all(std::vector<alert*>& alerts)
			{
				alerts.clear();

				// after this libtorrent threads can begin work on posting
				// new alerts if the queue was full.
				const int n_alerts = m_size.exchange(0);

				// pop n_alerts
				for (int i = 0; i < n_alerts; i++)
				{
					// remove the alert and mark the ring buffer slot as free
					alert * const alert = pop();
					TORRENT_ASSERT(alert != NULL);
					alerts.push_back(alert);
				}

				return n_alerts;
			}

		private:
			// pops the next alert from the ring buffer. The caller of
			// this function must ensure that there is at least one alert
			// in the ring buffer before making this call
			alert* pop()
			{
				const int size_limit = m_size_limit;
				int slot = m_read_slot;
				alert* alert;

				// with multiple threads postings alerts slots may be written
				// out of sequence so we must check that the alert is there
				// before popping it.
				for (int spins = 0; (alert = m_storage[slot]) == NULL; spins++)
					if (spins > TORRENT_ALERT_MANAGER_SPIN_MAX) boost::this_thread::yield();
				TORRENT_ASSERT(alert != NULL);
				m_storage[slot++] = NULL;

				if (slot == (size_limit * 2))
					slot = 0;

				TORRENT_ASSERT(slot >= 0 && slot < (size_limit * 2));

				m_read_slot = slot;

				return alert;
			}

			// this is where all alerts are queued up. We use a single vector of
			// alert pointers as a ring buffer. Each slot is accessed atomically
			// so no locking is necessary. m_write_slot and m_read_slot
			// are used to track the back and front slots respectively. m_size
			// tracks the number of pointers in the queue.
			boost::atomic<alert*>* m_storage;

			// this is used to track the number of alerts in the ring buffer
			boost::atomic<int> m_size;

			// this are used to track the ring buffer back and front slots
			boost::atomic<int> m_write_slot;
			boost::atomic<int> m_read_slot;

			// the current size of the ringbuffer
			int m_size_limit;
		};

		// manages the stack_allocators for each thread
		class thread_storage
		{
		public:
			thread_storage() : m_generation(0) {}

			// gets the current allocator
			aux::stack_allocator& current_allocator()
			{
				return m_allocations[m_generation.load()];
			}

			// this function swaps the allocators for the thread
			// that owns this object. Only one thread can call this
			// function concurrently, but it is ok to call it while
			// the owner thread is accessing it.
			void swap_allocators()
			{
				int index = m_generation.load();

				// if the allocator is not dirty don't swap
				if (!m_allocations[index].dirty())
					return;

				// swap allocators
				if (++index == 3) index = 0;
				m_allocations[index].reset();
				m_generation.store(index);
				TORRENT_ASSERT(index < 3);
			}

		private:
			aux::stack_allocator m_allocations[3];
			boost::atomic<int> m_generation;
		};

		public:
		alert_manager(int queue_limit
			, boost::uint32_t alert_mask = alert::error_notification);
		~alert_manager();

#if !defined TORRENT_FORCE_CXX98_EMPLACE_ALERT \
	&& !defined BOOST_NO_CXX11_VARIADIC_TEMPLATES \
	&& !defined BOOST_NO_CXX11_RVALUE_REFERENCES

		template <class T, typename... Args>
		bool emplace_alert(Args&&... args)
		{
			// acquire a shared lock
			shared_lock::scoped_lock lock(m_shared_lock, shared_lock::shared);

			// allocate thread specific storage the first time the thread runs
			// this will be freed automagically by boost::thread_specific_ptr
			if (m_thread_storage.get() == NULL)
				init_thread_storage();

			// get the thread specific storage
			thread_storage*const ts = m_thread_storage.get();

#ifndef TORRENT_NO_DEPRECATE
			if (m_dispatch)
			{
				m_dispatch(std::auto_ptr<alert>(new T(ts->current_allocator()
					, std::forward<Args>(args)...)));
				return false;
			}
#endif
			int index;
			T* alert;
			alert = m_alerts_pool.acquire(alert);
			new (alert) T(ts->current_allocator()
				, std::forward<Args>(args)...);
			TORRENT_ASSERT(alert != NULL);

			if ((index = m_alerts.push(alert, T::priority)) == -1)
			{
#ifndef TORRENT_DISABLE_EXTENSIONS
				if (!m_ses_extensions_reliable.empty())
					notify_extensions(alert, m_ses_extensions_reliable);
#endif
				// free the alert
				m_alerts_pool.release(alert);
				return false;
			}

			if (index == 0)
			{
				// we just posted to an empty queue. If anyone is waiting for
				// alerts, we need to notify them. Also (potentially) call the
				// user supplied m_notify callback to let the client wake up its
				// message loop to poll for alerts.
				if (m_notify) m_notify();

				// wake any threads waiting for alerts
				mutex::scoped_lock lock(m_mutex);
				m_condition.notify_all();
			}

#ifndef TORRENT_DISABLE_EXTENSIONS
			notify_extensions(alert, m_ses_extensions);
#endif

			return true;
		}
#else

// emulate variadic templates for c++98

#include "libtorrent/aux_/alert_manager_variadic_emplace.hpp"

#endif

		void get_all(std::vector<alert*>& alerts);

		template <class T>
		bool should_post() const
		{
			// the only reason m_alert_mask is atomic is because if it wasn't there
			// could be a "data race" here as defined by the C++11 standard on some
			// architectures (mostly 16-bit ones). On those architectures we may see
			// some visible side-effects from set_alert_mask() on an undefined order.
			// In other words some bits may change before others. But since we're only
			// concerned about individual bits here, and those are always atomic, there
			// would be no *race condition* and the behaviour of our program is well
			// defined on all architectures.
			//
			// So using a relaxed load eliminates the possible "data race" but should
			// generate exactly the same code as a non-atimic variable on our targets.
			//
			// The existing barriers on emplace_alert() will ensure that bit changes
			// from 1 to 0 are seen immediately.
			return (m_alert_mask.load(boost::memory_order_relaxed) & T::static_category);
		}

		alert* wait_for_alert(time_duration max_wait);

		void set_alert_mask(boost::uint32_t m)
		{
			// we don't care if this store happens immediately
			// because it is guaranteed to happen before the caller
			// makes any attempt to synchronize
			m_alert_mask.store(m, boost::memory_order_relaxed);
		}

		boost::uint32_t alert_mask() const
		{
			return m_alert_mask.load(boost::memory_order_relaxed);
		}

		int alert_queue_size_limit() const { return m_alerts.size_limit(); }
		int set_alert_queue_size_limit(int queue_size_limit_);

		void set_notify_function(boost::function<void()> const& fun);

#ifndef TORRENT_NO_DEPRECATE
		void set_dispatch_function(boost::function<void(std::auto_ptr<alert>)> const&);
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<plugin> ext);
#endif

	private:

		// non-copyable
		alert_manager(alert_manager const&);
		alert_manager& operator=(alert_manager const&);

		void init_thread_storage()
		{
			mutex::scoped_lock lock(m_mutex);

			// allocate thread storage and also make it available
			// to all threads through the m_threads_storage vector
			m_thread_storage.reset(new thread_storage());
			m_threads_storage.push_back(m_thread_storage.get());
		}

		void maybe_resize_buffer();

		mutable mutex m_mutex;
		condition_variable m_condition;
		boost::atomic<boost::uint32_t> m_alert_mask;

#ifndef TORRENT_NO_DEPRECATE
		boost::function<void(std::auto_ptr<alert>)> m_dispatch;
#endif

		// this function (if set) is called whenever the number of alerts in
		// the alert queue goes from 0 to 1. The client is expected to wake up
		// its main message loop for it to poll for alerts (using get_alerts()).
		// That call will drain every alert in one atomic operation and this
		// notification function will be called again the next time an alert is
		// posted to the queue
		boost::function<void()> m_notify;

		// pool of malloc'd alerts
		alert_pool m_alerts_pool;

		// this is where alert pointers are enqueued for get_all()
		lockfree_queue m_alerts;

		// this is where we store alert pointers after the user pops them from
		// the ring buffer so that we can release them on the next call to get_all()
		// and during destruction.
		std::vector<alert*> m_alerts_pending_delete;

		// when the user calls set_queue_size_limit it will set this value to the
		// new limit but the actual resize may not be done until the next time there
		// is an opportunity to do it on get_all()
		int m_queue_limit_requested;

		// this is used to gain exclusive access to the internal queue structures
		mutable shared_lock m_shared_lock;

		// this hold a pointer to thread specific storage for the current thread
		boost::thread_specific_ptr<thread_storage> m_thread_storage;

		// this makes the thread specific storage available to other threads
		std::vector<thread_storage*> m_threads_storage;

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<plugin> > ses_extension_list_t;

		void notify_extensions(alert * const alert, ses_extension_list_t const& list)
		{
			for (ses_extension_list_t::const_iterator i = list.begin(),
				end(list.end()); i != end; ++i)
			{
				TORRENT_TRY
				{
					(*i)->on_alert(alert);
				}
				TORRENT_CATCH (std::exception& ex)
				{
					(void) ex;
				}
			}
		}

		ses_extension_list_t m_ses_extensions;
		ses_extension_list_t m_ses_extensions_reliable;
#endif
	};
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif

