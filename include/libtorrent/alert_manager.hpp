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
#include <boost/shared_ptr.hpp>
#include <boost/config.hpp>
#include <list>
#include <utility> // for std::forward

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef __GNUC__
// this is to suppress the warnings for using std::auto_ptr
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// used for emplace_alert() variadic template emulation for c++98
#define TORRENT_ALERT_MANAGER_MAX_ARITY 7

namespace libtorrent {

#ifndef TORRENT_DISABLE_EXTENSIONS
	struct plugin;
#endif

	class TORRENT_EXTRA_EXPORT alert_manager
	{
	public:
		alert_manager(int queue_limit
			, boost::uint32_t alert_mask = alert::error_notification);
		~alert_manager();

#ifndef BOOST_NO_CXX11_VARIADIC_TEMPLATES

		template <class T, typename... Args>
		void emplace_alert(Args&&... args)
		{
			mutex::scoped_lock lock(m_mutex);
#ifndef TORRENT_NO_DEPRECATE
			if (m_dispatch)
			{
				m_dispatch(std::auto_ptr<alert>(new T(m_allocations[m_generation]
					, std::forward<Args>(args)...)));
				return;
			}
#endif
			// don't add more than this number of alerts, unless it's a
			// high priority alert, in which case we try harder to deliver it
			// for high priority alerts, double the upper limit
			if (m_alerts[m_generation].size() >= m_queue_size_limit
				* (1 + T::priority))
				return;

			T alert(m_allocations[m_generation], std::forward<Args>(args)...);
			m_alerts[m_generation].push_back(alert);

			maybe_notify(&alert, lock);
		}

#else

// emulate variadic templates for c++98

#include "libtorrent/aux_/alert_manager_variadic_emplace.hpp"

#endif

		bool pending() const;
		void get_all(std::vector<alert*>& alerts);

		template <class T>
		bool should_post() const
		{
			mutex::scoped_lock lock(m_mutex);
			if (m_alerts[m_generation].size() >= m_queue_size_limit
				* (1 + T::priority))
			{
				return false;
			}
			return (m_alert_mask & T::static_category) != 0;
		}

		alert* wait_for_alert(time_duration max_wait);

		void set_alert_mask(boost::uint32_t m)
		{
			mutex::scoped_lock lock(m_mutex);
			m_alert_mask = m;
		}

		boost::uint32_t alert_mask() const
		{
			mutex::scoped_lock lock(m_mutex);
			return m_alert_mask;
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

		void maybe_notify(alert* a, mutex::scoped_lock& lock);

		mutable mutex m_mutex;
		condition_variable m_condition;
		boost::uint32_t m_alert_mask;
		int m_queue_size_limit;

#ifndef TORRENT_NO_DEPRECATE
		bool maybe_dispatch(alert const& a);
		boost::function<void(std::auto_ptr<alert>)> m_dispatch;
#endif

		// this function (if set) is called whenever the number of alerts in
		// the alert queue goes from 0 to 1. The client is expected to wake up
		// its main message loop for it to poll for alerts (using get_alerts()).
		// That call will drain every alert in one atomic operation and this
		// notification function will be called again the next time an alert is
		// posted to the queue
		boost::function<void()> m_notify;

		// this is either 0 or 1, it indicates which m_alerts and m_allocations
		// the alert_manager is allowed to use right now. This is swapped when
		// the client calls get_all(), at which point all of the alert objects
		// passed to the client will be owned by libtorrent again, and reset.
		int m_generation;

		// this is where all alerts are queued up. There are two heterogenous
		// queues to double buffer the thread access. The mutex in the alert
		// manager gives exclusive access to m_alerts[m_generation] and
		// m_allocations[m_generation] whereas the other copy is exclusively
		// used by the client thread.
		heterogeneous_queue<alert> m_alerts[2];

		// this is a stack where alerts can allocate variable length content,
		// such as strings, to go with the alerts.
		aux::stack_allocator m_allocations[2];

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<plugin> > ses_extension_list_t;
		ses_extension_list_t m_ses_extensions;
#endif
	};
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif

