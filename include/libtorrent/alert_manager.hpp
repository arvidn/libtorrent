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
#include "libtorrent/heterogeneous_queue.hpp"
#include "libtorrent/stack_allocator.hpp"

#include <functional>
#include <list>
#include <utility> // for std::forward
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace libtorrent {

#ifndef TORRENT_DISABLE_EXTENSIONS
	struct plugin;
#endif

	class TORRENT_EXTRA_EXPORT alert_manager
	{
	public:
		alert_manager(int queue_limit
			, std::uint32_t alert_mask = alert::error_notification);
		~alert_manager();

		template <class T, typename... Args>
		void emplace_alert(Args&&... args)
		{
			std::unique_lock<std::mutex> lock(m_mutex);

			// don't add more than this number of alerts, unless it's a
			// high priority alert, in which case we try harder to deliver it
			// for high priority alerts, double the upper limit
			if (m_alerts[m_generation].size() >= m_queue_size_limit
				* (1 + T::priority))
				return;

			T& alert = m_alerts[m_generation].emplace_back<T>(
				m_allocations[m_generation], std::forward<Args>(args)...);

			maybe_notify(&alert, lock);
		}

		bool pending() const;
		void get_all(std::vector<alert*>& alerts);

		template <class T>
		bool should_post() const
		{
			if ((m_alert_mask.load(std::memory_order_relaxed)
				& T::static_category) == 0)
			{
				return false;
			}

			return should_post_impl(T::priority);
		}

		alert* wait_for_alert(time_duration max_wait);

		void set_alert_mask(std::uint32_t m)
		{
			m_alert_mask = m;
		}

		std::uint32_t alert_mask() const
		{
			return m_alert_mask;
		}

		int alert_queue_size_limit() const { return m_queue_size_limit; }
		int set_alert_queue_size_limit(int queue_size_limit_);

		void set_notify_function(std::function<void()> const& fun);

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(std::shared_ptr<plugin> ext);
#endif

	private:

		// non-copyable
		alert_manager(alert_manager const&);
		alert_manager& operator=(alert_manager const&);

		bool should_post_impl(int priority) const;
		void maybe_notify(alert* a, std::unique_lock<std::mutex>& lock);

		mutable std::mutex m_mutex;
		std::condition_variable m_condition;
		std::atomic<std::uint32_t> m_alert_mask;
		int m_queue_size_limit;

		// this function (if set) is called whenever the number of alerts in
		// the alert queue goes from 0 to 1. The client is expected to wake up
		// its main message loop for it to poll for alerts (using get_alerts()).
		// That call will drain every alert in one atomic operation and this
		// notification function will be called again the next time an alert is
		// posted to the queue
		std::function<void()> m_notify;

		// this is either 0 or 1, it indicates which m_alerts and m_allocations
		// the alert_manager is allowed to use right now. This is swapped when
		// the client calls get_all(), at which point all of the alert objects
		// passed to the client will be owned by libtorrent again, and reset.
		int m_generation;

		// this is where all alerts are queued up. There are two heterogeneous
		// queues to double buffer the thread access. The std::mutex in the alert
		// manager gives exclusive access to m_alerts[m_generation] and
		// m_allocations[m_generation] whereas the other copy is exclusively
		// used by the client thread.
		heterogeneous_queue<alert> m_alerts[2];

		// this is a stack where alerts can allocate variable length content,
		// such as strings, to go with the alerts.
		aux::stack_allocator m_allocations[2];

#ifndef TORRENT_DISABLE_EXTENSIONS
		std::list<std::shared_ptr<plugin>> m_ses_extensions;
#endif
	};
}

#endif
