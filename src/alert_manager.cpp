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
		, m_num_queued_resume(0)
		, m_generation(0)
	{}

	alert_manager::~alert_manager() {}

	int alert_manager::num_queued_resume() const
	{
		mutex::scoped_lock lock(m_mutex);
		return m_num_queued_resume;
	}

	alert* alert_manager::wait_for_alert(time_duration max_wait)
	{
		mutex::scoped_lock lock(m_mutex);

		if (!m_alerts[m_generation].empty())
			return m_alerts[m_generation].front();

		// this call can be interrupted prematurely by other signals
		m_condition.wait_for(lock, max_wait);
		if (!m_alerts[m_generation].empty())
			return m_alerts[m_generation].front();

		return NULL;
	}

	void alert_manager::maybe_notify(alert* a, mutex::scoped_lock& lock)
	{
		if (a->type() == save_resume_data_failed_alert::alert_type
			|| a->type() == save_resume_data_alert::alert_type)
			++m_num_queued_resume;

		if (m_alerts[m_generation].size() == 1)
		{
			lock.unlock();

			// we just posted to an empty queue. If anyone is waiting for
			// alerts, we need to notify them. Also (potentially) call the
			// user supplied m_notify callback to let the client wake up its
			// message loop to poll for alerts.
			if (m_notify) m_notify();

			// TODO: 2 keep a count of the number of threads waiting. Only if it's
			// > 0 notify them
			m_condition.notify_all();
		}
		else
		{
			lock.unlock();
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (ses_extension_list_t::iterator i = m_ses_extensions.begin()
			, end(m_ses_extensions.end()); i != end; ++i)
		{
			(*i)->on_alert(a);
		}
#endif
	}

#ifndef TORRENT_NO_DEPRECATE

	bool alert_manager::maybe_dispatch(alert const& a)
	{
		if (m_dispatch)
		{
			m_dispatch(a.clone());
			return true;
		}
		return false;
	}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	void alert_manager::set_dispatch_function(
		boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		mutex::scoped_lock lock(m_mutex);

		m_dispatch = fun;

		heterogeneous_queue<alert> storage;
		m_alerts[m_generation].swap(storage);
		lock.unlock();

		std::vector<alert*> alerts;
		storage.get_pointers(alerts);

		for (std::vector<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			m_dispatch((*i)->clone());
		}
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif

	void alert_manager::set_notify_function(boost::function<void()> const& fun)
	{
		mutex::scoped_lock lock(m_mutex);
		m_notify = fun;
		if (!m_alerts[m_generation].empty())
		{
			// never call a callback with the lock held!
			lock.unlock();
			if (m_notify) m_notify();
		}
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void alert_manager::add_extension(boost::shared_ptr<plugin> ext)
	{
		m_ses_extensions.push_back(ext);
	}
#endif

	void alert_manager::get_all(std::vector<alert*>& alerts, int& num_resume)
	{
		mutex::scoped_lock lock(m_mutex);
		TORRENT_ASSERT(m_num_queued_resume <= m_alerts[m_generation].size());

		alerts.clear();
		if (m_alerts[m_generation].empty()) return;

		m_alerts[m_generation].get_pointers(alerts);
		num_resume = m_num_queued_resume;
		m_num_queued_resume = 0;

		// swap buffers
		m_generation = (m_generation + 1) & 1;
		// clear the one we will start writing to now
		m_alerts[m_generation].clear();
		m_allocations[m_generation].reset();
	}

	bool alert_manager::pending() const
	{
		mutex::scoped_lock lock(m_mutex);
		return !m_alerts[m_generation].empty();
	}

	int alert_manager::set_alert_queue_size_limit(int queue_size_limit_)
	{
		mutex::scoped_lock lock(m_mutex);

		std::swap(m_queue_size_limit, queue_size_limit_);
		return queue_size_limit_;
	}

}

