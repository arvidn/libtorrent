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

#include "libtorrent/config.hpp"
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/alert_types.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS
#include "libtorrent/extensions.hpp"
#endif

namespace libtorrent
{

	alert_manager::alert_manager(int queue_limit, boost::uint32_t alert_mask)
		: m_alert_mask(alert_mask)
		, m_queue_size_limit(queue_limit)
	{}

	alert_manager::~alert_manager()
	{
		while (!m_alerts.empty())
		{
			TORRENT_ASSERT(alert_cast<save_resume_data_alert>(m_alerts.front()) == 0
				&& "shutting down session with remaining resume data alerts in the alert queue. "
				"You proabably wany to make sure you always wait for all resume data "
				"alerts before shutting down");
			delete m_alerts.front();
			m_alerts.pop_front();
		}
	}

	alert const* alert_manager::wait_for_alert(time_duration max_wait)
	{
		mutex::scoped_lock lock(m_mutex);

		if (!m_alerts.empty()) return m_alerts.front();
		
		// this call can be interrupted prematurely by other signals
		m_condition.wait_for(lock, max_wait);
		if (!m_alerts.empty()) return m_alerts.front();

		return NULL;
	}

	void alert_manager::set_dispatch_function(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		mutex::scoped_lock lock(m_mutex);

		m_dispatch = fun;

		std::deque<alert*> alerts;
		m_alerts.swap(alerts);
		lock.unlock();

		while (!alerts.empty())
		{
			TORRENT_TRY {
				m_dispatch(std::auto_ptr<alert>(alerts.front()));
			} TORRENT_CATCH(std::exception&) {}
			alerts.pop_front();
		}
	}

	void dispatch_alert(boost::function<void(alert const&)> dispatcher
		, alert* alert_)
	{
		std::auto_ptr<alert> holder(alert_);
		dispatcher(*alert_);
	}

	void alert_manager::post_alert_ptr(alert* alert_)
	{
		std::auto_ptr<alert> a(alert_);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_alert(alert_);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif

		mutex::scoped_lock lock(m_mutex);
		post_impl(a, lock);
	}

	void alert_manager::post_alert(const alert& alert_)
	{
		std::auto_ptr<alert> a(alert_.clone());

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			TORRENT_TRY {
				(*i)->on_alert(&alert_);
			} TORRENT_CATCH(std::exception&) {}
		}
#endif

		mutex::scoped_lock lock(m_mutex);
		post_impl(a, lock);
	}
		
	void alert_manager::post_impl(std::auto_ptr<alert>& alert_, mutex::scoped_lock& l)
	{
		if (m_dispatch)
		{
			TORRENT_ASSERT(m_alerts.empty());
			TORRENT_TRY {
				m_dispatch(alert_);
			} TORRENT_CATCH(std::exception&) {}
		}
		else if (m_alerts.size() < m_queue_size_limit || !alert_->discardable())
		{
			m_alerts.push_back(alert_.release());
			if (m_alerts.size() == 1)
				m_condition.notify_all();
		}
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void alert_manager::add_extension(boost::shared_ptr<plugin> ext)
	{
		m_ses_extensions.push_back(ext);
	}
#endif

	std::auto_ptr<alert> alert_manager::get()
	{
		mutex::scoped_lock lock(m_mutex);
		
		if (m_alerts.empty())
			return std::auto_ptr<alert>(0);

		alert* result = m_alerts.front();
		m_alerts.pop_front();
		return std::auto_ptr<alert>(result);
	}

	void alert_manager::get_all(std::deque<alert*>* alerts)
	{
		mutex::scoped_lock lock(m_mutex);
		if (m_alerts.empty()) return;
		m_alerts.swap(*alerts);
	}

	bool alert_manager::pending() const
	{
		mutex::scoped_lock lock(m_mutex);
		
		return !m_alerts.empty();
	}

	size_t alert_manager::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		mutex::scoped_lock lock(m_mutex);

		std::swap(m_queue_size_limit, queue_size_limit_);
		return queue_size_limit_;
	}

}

