/*

Copyright (c) 2003-2014, Arvid Norberg, Daniel Wallin
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

#ifndef TORRENT_NO_DEPRECATE
#include <boost/function/function1.hpp>
#endif
#include <boost/function/function0.hpp>
#include <boost/shared_ptr.hpp>
#include <list>

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

		// TODO: 3 remove this
		template <class T>
		void post_alert_ptr(T const* a)
		{
			post_alert(*a);
			delete a;
		}

		// TODO: 2 instead of copying the alert, pass in all the constructor
		// arguments and construct it in place
		template <class T>
		void post_alert(T const& a)
		{
			mutex::scoped_lock lock(m_mutex);
#ifndef TORRENT_NO_DEPRECATE
			if (m_dispatch)
			{
				m_dispatch(a.clone());
				return;
			}
#endif
			if (m_alerts.size() >= m_queue_size_limit) return;

			m_alerts.push_back(a);

			if (m_alerts.size() == 1)
			{
				lock.unlock();

				// we just posted to an empty queue. If anyone is waiting for
				// alerts, we need to notify them. Also (potentially) call the
				// user supplied m_notify callback to let the client wake up its
				// message loop to poll for alerts.
				if (m_notify) m_notify();
				m_condition.notify_all();
			}
		}

		bool pending() const;
		void get_all(std::vector<alert*>& alerts, int& num_resume);

		template <class T>
		bool should_post() const
		{
			mutex::scoped_lock lock(m_mutex);
			if (m_alerts.size() >= m_queue_size_limit) return false;
			return (m_alert_mask & T::static_category) != 0;
		}

		bool should_post(alert const* a) const
		{ return (m_alert_mask & a->category()) != 0; }

		alert* wait_for_alert(time_duration max_wait);

		void set_alert_mask(boost::uint32_t m)
		{
			mutex::scoped_lock lock(m_mutex);
			m_alert_mask = m;
		}

		int alert_mask() const { return m_alert_mask; }

		size_t alert_queue_size_limit() const { return m_queue_size_limit; }
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

		void set_notify_function(boost::function<void()> const& fun);

#ifndef TORRENT_NO_DEPRECATE
		void set_dispatch_function(boost::function<void(std::auto_ptr<alert>)> const&);
#endif

		int num_queued_resume() const;

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<plugin> ext);
#endif

	private:

		// non-copyable
		alert_manager(alert_manager const&);
		alert_manager& operator=(alert_manager const&);

		mutable mutex m_mutex;
		condition_variable m_condition;
		boost::uint32_t m_alert_mask;
		size_t m_queue_size_limit;

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

		// the number of resume data alerts  in the alert queue
		int m_num_queued_resume;

		heterogeneous_queue<alert> m_alerts;

		// this is the copy of alerts belonging to the client thread. When the
		// clint asks for alerts, they are all pulled in here to be stored
		// and accessed by the user until another batch is pulled in. (at that
		// point these are swapped back into the alert_manager and destructed).
		heterogeneous_queue<alert> m_client_alerts;

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<plugin> > ses_extension_list_t;
		ses_extension_list_t m_ses_extensions;
#endif
	};
}

#endif

