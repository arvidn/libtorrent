/*

Copyright (c) 2003, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/pch.hpp"

#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include <boost/thread/xtime.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

namespace libtorrent {

	alert::alert() : m_timestamp(time_now()) {}
	alert::~alert() {}
	ptime alert::timestamp() const { return m_timestamp; }

	alert_manager::alert_manager(io_service& ios)
		: m_alert_mask(alert::error_notification)
		, m_queue_size_limit(queue_size_limit_default)
		, m_ios(ios)
	{}

	alert_manager::~alert_manager()
	{
		while (!m_alerts.empty())
		{
			delete m_alerts.front();
			m_alerts.pop();
		}
	}

	alert const* alert_manager::wait_for_alert(time_duration max_wait)
	{
		boost::mutex::scoped_lock lock(m_mutex);

		if (!m_alerts.empty()) return m_alerts.front();
		
		int secs = total_seconds(max_wait);
		max_wait -= seconds(secs);
		boost::xtime xt;
		boost::xtime_get(&xt, boost::TIME_UTC);
		xt.sec += secs;
		boost::int64_t nsec = xt.nsec + total_microseconds(max_wait) * 1000;
		if (nsec > 1000000000)
		{
			nsec -= 1000000000;
			xt.sec += 1;
		}
		xt.nsec = boost::xtime::xtime_nsec_t(nsec);
		// apparently this call can be interrupted
		// prematurely if there are other signals
		while (m_condition.timed_wait(lock, xt))
			if (!m_alerts.empty()) return m_alerts.front();

		return 0;
	}

	void alert_manager::set_dispatch_function(boost::function<void(alert const&)> const& fun)
	{
		boost::mutex::scoped_lock lock(m_mutex);

		m_dispatch = fun;

		std::queue<alert*> alerts = m_alerts;
		while (!m_alerts.empty()) m_alerts.pop();
		lock.unlock();

		while (!alerts.empty())
		{
			m_dispatch(*alerts.front());
			delete alerts.front();
			alerts.pop();
		}
	}

	void dispatch_alert(boost::function<void(alert const&)> dispatcher
		, alert* alert_)
	{
		std::auto_ptr<alert> holder(alert_);
		dispatcher(*alert_);
	}

	void alert_manager::post_alert(const alert& alert_)
	{
		boost::mutex::scoped_lock lock(m_mutex);

		if (m_dispatch)
		{
			TORRENT_ASSERT(m_alerts.empty());
			m_ios.post(boost::bind(&dispatch_alert, m_dispatch, alert_.clone().release()));
			return;
		}

		if (m_alerts.size() >= m_queue_size_limit) return;
		m_alerts.push(alert_.clone().release());
		m_condition.notify_all();
	}

	std::auto_ptr<alert> alert_manager::get()
	{
		boost::mutex::scoped_lock lock(m_mutex);
		
		TORRENT_ASSERT(!m_alerts.empty());

		alert* result = m_alerts.front();
		m_alerts.pop();
		return std::auto_ptr<alert>(result);
	}

	bool alert_manager::pending() const
	{
		boost::mutex::scoped_lock lock(m_mutex);
		
		return !m_alerts.empty();
	}

	size_t alert_manager::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		boost::mutex::scoped_lock lock(m_mutex);

		std::swap(m_queue_size_limit, queue_size_limit_);
		return queue_size_limit_;
	}

	stats_alert::stats_alert(torrent_handle const& h, int in
		, stat const& s)
		: torrent_alert(h)
		, interval(in)
	{
		for (int i = 0; i < num_channels; ++i)
			transferred[i] = s[i].counter();
	}

	std::string stats_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: [%d] %d %d %d %d %d %d %d %d %d %d"
			, torrent_alert::message().c_str()
			, interval
			, transferred[0]
			, transferred[1]
			, transferred[2]
			, transferred[3]
			, transferred[4]
			, transferred[5]
			, transferred[6]
			, transferred[7]
			, transferred[8]
			, transferred[9]);
		return msg;
	}

	cache_flushed_alert::cache_flushed_alert(torrent_handle const& h): torrent_alert(h) {}

} // namespace libtorrent

