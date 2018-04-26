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

// the number of threads that can emplace alerts concurrently
// if this limit is exceeded everything will still be thread
// safe but there will be contention over the allocators
#define TORRENT_ALERT_MANAGER_N_GENERATIONS	(4)

// the maximum number of cycles to a spinlock may spin
// before yielding the cpu
#define TORRENT_ALERT_MANAGER_SPIN_MAX		(20)

namespace libtorrent {

#ifndef TORRENT_DISABLE_EXTENSIONS
	struct plugin;
#endif

	class TORRENT_EXTRA_EXPORT alert_manager
	{
		enum atomic_enqueue_result_t
		{
			SUCCESS,
			QUEUE_FULL,
			TIMEOUT
		};

		// this is used to track the state of each thread that calls
		// emplace_alert()
		struct thread_storage
		{
		public:
			thread_storage() : youngest_generation(0)
				, oldest_generation(0), n_generations(0) {}

			// gets the current generation
			int current_generation()
			{
				return youngest_generation.load();
			}

			// gets the current allocator
			aux::stack_allocator& current_allocator()
			{
				return allocations[youngest_generation.load()];
			}

			// gets an allocator
			aux::stack_allocator& get_allocator(const int index)
			{
				TORRENT_ASSERT(index >= 0 && index < TORRENT_ALERT_MANAGER_N_GENERATIONS);
				return allocations[youngest_generation];
			}

			// this function swaps the allocators for the thread
			// that owns this object. Only one thread can call this
			// function concurrently, but it is ok to call it while
			// the owner thread is accessing it.
			void swap_allocators()
			{
				int gen = youngest_generation.load();

				// get the next allocator index
				gen = (gen == TORRENT_ALERT_MANAGER_N_GENERATIONS - 1) ? 0 : (gen + 1);
				n_generations++;
				TORRENT_ASSERT(gen < TORRENT_ALERT_MANAGER_N_GENERATIONS);
				TORRENT_ASSERT(oldest_generation <= TORRENT_ALERT_MANAGER_N_GENERATIONS);

				// delete the oldest generation
				if (n_generations == TORRENT_ALERT_MANAGER_N_GENERATIONS)
				{
					TORRENT_ASSERT(gen == oldest_generation);
					allocations[oldest_generation].reset();
					oldest_generation = (oldest_generation == TORRENT_ALERT_MANAGER_N_GENERATIONS - 1) ? 0
						: oldest_generation + 1;
					n_generations--;
				}

				TORRENT_ASSERT(n_generations <= TORRENT_ALERT_MANAGER_N_GENERATIONS);

				// swap allocators
				youngest_generation.store(gen);
			}

		private:
			aux::stack_allocator allocations[TORRENT_ALERT_MANAGER_N_GENERATIONS];
			boost::atomic<int> youngest_generation;
			int oldest_generation;
			int n_generations;
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
				aux::stack_allocator::scoped_lock lock(ts->current_allocator());
				m_dispatch(std::auto_ptr<alert>(new T(lock.allocator()
					, std::forward<Args>(args)...)));
				return false;
			}
#endif

			// don't add more than this number of alerts, unless it's a
			// high priority alert, in which case we try harder to deliver it
			// for high priority alerts, double the upper limit.
			if (m_queue_size.load() >= (m_queue_size_limit * (1 + T::priority)))
			{
#ifndef TORRENT_DISABLE_EXTENSIONS
				if (!m_ses_extensions_reliable.empty())
				{
					aux::stack_allocator::scoped_lock lock(ts->current_allocator());
					T alert(lock.allocator()
						, std::forward<Args>(args)...);
					notify_extensions(&alert, m_ses_extensions_reliable);
				}
#endif
				return false;
			}

			do
			{
				int ret;
				const int gen = ts->current_generation();
				aux::stack_allocator::scoped_lock lock(ts->get_allocator(gen), false);

				T* alert = new T(lock.allocator()
					, std::forward<Args>(args)...);

				if ((ret = do_enqueue_alert(alert, T::priority, gen)) != SUCCESS)
				{
					// free the alert
					delete alert;

					// this is just a safety net in case alerts where
					// popped twice while do_enqueue_alert() was running.
					if (ret == TIMEOUT) continue;
					TORRENT_ASSERT(ret == QUEUE_FULL);
#ifndef TORRENT_DISABLE_EXTENSIONS
					if (!m_ses_extensions_reliable.empty())
					{
						aux::stack_allocator::scoped_lock lock(ts->current_allocator());
						T alert(lock.allocator()
							, std::forward<Args>(args)...);
						notify_extensions(&alert, m_ses_extensions_reliable);
					}
#endif
					return false;
				}

#ifndef TORRENT_DISABLE_EXTENSIONS
				notify_extensions(alert, m_ses_extensions);
#endif

				return true;
			}
			while (1);
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
			shared_lock::scoped_lock lock(m_shared_lock, shared_lock::exclusive);
			return m_alert_mask.load(boost::memory_order_relaxed);
		}

		int alert_queue_size_limit() const { return m_queue_size_limit; }
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
			// to all threads through the m_threads_storage list
			m_thread_storage.reset(new thread_storage());
			m_threads_storage.push_back(m_thread_storage.get());
		}

		// tries to emplace a pre-constructed alert. Returns false if the
		// queue fills up before the alert can be posted true otherwise.
		int do_enqueue_alert(alert * const a, const int priority, const int& gen)
		{
			int current, next;
			alert* expected;

			TORRENT_ASSERT(a != NULL);

			// save local copy of limit
			const int size_limit = m_queue_size_limit;

			TORRENT_ASSERT((size_limit * (1 + priority)) <= (size_limit * 2));

			// here we're reserving the next available queue slot atomically.
			// the actual write is done next on the spinlock but the alert does
			// not become visible to get_all() until we increment m_queue_size
			do
			{
				// get the next slot on the ring buffer
				next = (current = m_queue_write_slot.load()) + 1;
				if (next == (size_limit * 2))
					next = 0;

				// calculate the real queue size, counting alerts that are not
				// yet included in m_queue_size because other threads are already
				// passed this check but not yet incremented m_queue_size. It is
				// important that m_queue_read_slot is loaded after m_queue_write_slot
				// or we may get the wrong result.
				const int read_slot = m_queue_read_slot.load();
				const int real_size = (next > read_slot) ? (next - read_slot)
					: ((next < read_slot) ? ((size_limit * 2) - (read_slot - next))
					: m_queue_size.load());

				// make sure that posting this alert will not exceed
				// the limit
				if (real_size >= size_limit * (1 + priority))
					return QUEUE_FULL;

				TORRENT_ASSERT(next >= 0 && next < (size_limit * 2));

				// if the generation changed twice since the alert was constructed
				// then abort as it's not safe.
				const int next_gen = gen + 2;
				const int next_gen_of = (gen + 1 >= TORRENT_ALERT_MANAGER_N_GENERATIONS) ? 1 : 0;
				const int c_gen = m_thread_storage->current_generation();
				if (c_gen >= next_gen || (c_gen < gen && c_gen >= next_gen_of))
				{
					TORRENT_ASSERT(false);
					return TIMEOUT;
				}
			}
			while (!m_queue_write_slot.compare_exchange_strong(current, next));

			// if an alert was just popped from this slot it is possible that get_all()
			// has not yet freed the queue slot. This is extremely unlikely with a properly
			// tuned m_queue_size. If it does happens it should never put the thread off
			// cpu unless the user thread is preempted before it frees the slot. Even then
			// we're guaranteed to make progress again after only 1 scheduler cycle.
			for (int spins = 0; !m_alerts[next].compare_exchange_strong((expected = NULL), a); spins++)
				if (spins >= TORRENT_ALERT_MANAGER_SPIN_MAX) std::this_thread::yield();

			// this makes the alert visible to get_all()
			current = m_queue_size.fetch_add(1);

			if (current == 0)
			{
				// we just posted to an empty queue. If anyone is waiting for
				// alerts, we need to notify them. Also (potentially) call the
				// user supplied m_notify callback to let the client wake up its
				// message loop to poll for alerts.
				if (m_notify) m_notify();

				// wake any threads waiting for alerts
				m_condition.notify_all();
			}

			// release the ring buffer
			TORRENT_ASSERT(next <= (size_limit * 2));
			TORRENT_ASSERT(m_queue_size_limit == size_limit);
			return SUCCESS;
		}

		void maybe_resize_buffer();
		alert* pop_alert();

		mutable mutex m_mutex;
		condition_variable m_condition;
		boost::atomic<boost::uint32_t> m_alert_mask;
		int m_queue_size_limit;

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

		// this is where all alerts are queued up. We use a single vector of
		// alert pointers as a ring buffer. Each slot is accessed atomically
		// so no locking is necessary. m_queue_write_slot and m_queue_read_slot
		// are used to track the back and front slots respectively. m_queue_size
		// tracks the number of pointers in the queue.
		std::vector<boost::atomic<alert*>> m_alerts;

		// this is used to track the number of alerts in the ring buffer
		boost::atomic<int> m_queue_size;

		// this are used to track the ring buffer back and front slots
		boost::atomic<int> m_queue_write_slot;
		boost::atomic<int> m_queue_read_slot;

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

		// tracks the threads popping alerts
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

