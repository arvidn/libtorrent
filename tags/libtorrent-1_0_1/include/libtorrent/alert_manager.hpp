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

#include <boost/function/function1.hpp>
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

		void post_alert(const alert& alert_);
		void post_alert_ptr(alert* alert_);
		bool pending() const;
		std::auto_ptr<alert> get();
		void get_all(std::deque<alert*>* alerts);

		template <class T>
		bool should_post() const
		{
			mutex::scoped_lock lock(m_mutex);
			if (m_alerts.size() >= m_queue_size_limit) return false;
			return (m_alert_mask & T::static_category) != 0;
		}

		bool should_post(alert const* a) const
		{
			return (m_alert_mask & a->category()) != 0;
		}

		alert const* wait_for_alert(time_duration max_wait);

		void set_alert_mask(boost::uint32_t m)
		{
			mutex::scoped_lock lock(m_mutex);
			m_alert_mask = m;
		}

		int alert_mask() const { return m_alert_mask; }

		size_t alert_queue_size_limit() const { return m_queue_size_limit; }
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

		void set_dispatch_function(boost::function<void(std::auto_ptr<alert>)> const&);

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<plugin> ext);
#endif

	private:
		void post_impl(std::auto_ptr<alert>& alert_, mutex::scoped_lock& l);

		std::deque<alert*> m_alerts;
		mutable mutex m_mutex;
		condition_variable m_condition;
		boost::uint32_t m_alert_mask;
		size_t m_queue_size_limit;
		boost::function<void(std::auto_ptr<alert>)> m_dispatch;

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<plugin> > ses_extension_list_t;
		ses_extension_list_t m_ses_extensions;
#endif
	};

}

#endif

