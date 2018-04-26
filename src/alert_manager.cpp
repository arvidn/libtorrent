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

namespace libtorrent
{

	alert_manager::alert_manager(int queue_limit, boost::uint32_t alert_mask)
		: m_alert_mask(alert_mask)
		, m_queue_size_limit(queue_limit)
#ifndef TORRENT_NO_DEPRECATE
		, m_dispatch(NULL)
#endif
		, m_notify(NULL)
		, m_alerts(queue_limit * 2)
		, m_queue_size(0)
		, m_queue_write_slot(-1)
		, m_queue_read_slot(0)
		, m_queue_limit_requested(-1)
	{
		for (int i = 0; i < queue_limit * 2; i++)
			m_alerts[i] = NULL;
	}

	alert_manager::~alert_manager()
	{
		mutex::scoped_lock lock(m_mutex);
		shared_lock::scoped_lock wlock(m_shared_lock, shared_lock::exclusive);

		const int n_alerts = m_queue_size;

		// release alerts in the pending deletes list
		for (unsigned int i = 0; i < m_alerts_pending_delete.size(); i++)
			delete m_alerts_pending_delete[i];

		// free alerts already in the queue
		for (int i = 0; i < n_alerts; i++)
		{
			alert * const alert = pop_alert();
			TORRENT_ASSERT(alert != NULL);
			delete alert;
		}
	}

	alert* alert_manager::wait_for_alert(time_duration max_wait)
	{
		mutex::scoped_lock lock(m_mutex);
		shared_lock::scoped_lock rlock(m_shared_lock, shared_lock::shared);

		if (m_queue_size > 0)
			return m_alerts[m_queue_read_slot];

		// this call can be interrupted prematurely by other signals
		rlock.unlock();
		m_condition.wait_for(lock, max_wait);
		rlock.lock();

		if (m_queue_size > 0)
			return m_alerts[m_queue_read_slot];

		return NULL;
	}

	// pops the next alert from the ring buffer. The caller of
	// this function must ensure that there is at least one alert
	// in the ring buffer before making this call
	alert * alert_manager::pop_alert()
	{
		const int size_limit = m_queue_size_limit;
		int slot = m_queue_read_slot;

		// with multiple threads postings alerts slots may be written
		// out of sequence so we must check that the alert is there
		// before popping it.
		for (int spins = 0; m_alerts[slot] == NULL; spins++)
			if (spins > TORRENT_ALERT_MANAGER_SPIN_MAX) std::this_thread::yield();

		alert * const alert = m_alerts[slot].load(boost::memory_order_relaxed);
		TORRENT_ASSERT(alert != NULL);
		m_alerts[slot++] = NULL;

		if (slot == (size_limit * 2))
			slot = 0;

		TORRENT_ASSERT(slot >= 0 && slot < (size_limit * 2));

		m_queue_read_slot = slot;

		return alert;
	}

#ifndef TORRENT_NO_DEPRECATE

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	void alert_manager::set_dispatch_function(
		boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		mutex::scoped_lock lock(m_mutex);
		shared_lock::scoped_lock wlock(m_shared_lock, shared_lock::exclusive);

		std::vector<alert*> alerts;

		for (int i = 0; i < m_queue_size; i++)
		{
			alert*const alert = pop_alert();
			TORRENT_ASSERT(alert != NULL);
			alerts.push_back(alert);
		}

		// clear the queue size and free ring buffer
		m_queue_size = 0;
		m_alerts = std::vector<boost::atomic<alert*>>(0);
		m_dispatch = fun;

		// unlock mutex and call dispatch function
		wlock.unlock();
		lock.unlock();

		for (unsigned int i = 0; i < alerts.size(); i++)
			m_dispatch(alerts[i]->clone());
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif

	void alert_manager::set_notify_function(boost::function<void()> const& fun)
	{
		mutex::scoped_lock lock(m_mutex);
		shared_lock::scoped_lock wlock(m_shared_lock, shared_lock::exclusive);

		m_notify = fun;

		// this load can be relaxed because it is guaranteed to happen
		// after the synchronized store above.
		if (m_queue_size.load(boost::memory_order_relaxed) != 0)
			if (m_notify) m_notify();
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void alert_manager::add_extension(boost::shared_ptr<plugin> ext)
	{
		shared_lock::scoped_lock wlock(m_shared_lock, shared_lock::exclusive);
		if ((ext->implemented_features() & plugin::reliable_alerts_feature) != 0)
			m_ses_extensions_reliable.push_back(ext);
		m_ses_extensions.push_back(ext);
	}
#endif

	void alert_manager::maybe_resize_buffer()
	{
		const int new_limit = m_queue_limit_requested;

		// we can only resize if there is no other thread posting alerts
		// and if no thread posted an alert since we popped them
		if (new_limit < 0 || m_queue_size > 0)
			return;

		// resize the queue
		m_alerts = std::vector<boost::atomic<alert*>>(new_limit * 2);
		for (int i = 0; i < new_limit * 2; i++)
			m_alerts[i].store(NULL, boost::memory_order_relaxed);

		m_queue_size_limit = new_limit;
		m_queue_write_slot = -1;
		m_queue_read_slot = 0;
		m_queue_limit_requested = -1;
	}

	void alert_manager::get_all(std::vector<alert*>& alerts)
	{
		mutex::scoped_lock lock(m_mutex);
		shared_lock::scoped_lock wlock(m_shared_lock, shared_lock::exclusive, false);

		// if a queue resize has been requested acquire an exclusive lock
		if (m_queue_limit_requested != -1)
			wlock.lock();

		// yield the cpu at least once before to ensure that
		// this function cannot be called twice in the timespan
		// between the alert contruction and it being placed in the
		// queue (and available to us).
		std::this_thread::yield();

		// release alerts in the pending deletes list
		for (unsigned int i = 0; i < m_alerts_pending_delete.size(); i++)
			delete m_alerts_pending_delete[i];
		m_alerts_pending_delete.clear();
		alerts.clear();

		// after this libtorrent threads can begin work on posting
		// new alerts if the queue was full.
		const int n_alerts = m_queue_size.exchange(0);
		if (n_alerts == 0) return;

		// pop n_alerts
		for (int i = 0; i < n_alerts; i++)
		{
			// remove the alert and mark the ring buffer slot as free
			alert * const alert = pop_alert();
			TORRENT_ASSERT(alert != NULL);

			// copy the alerts to the user's vector and to the
			// pending delete list
			m_alerts_pending_delete.push_back(alert);
			alerts.push_back(alert);
		}

		// if a buffer resize is pending do it now
		if (m_queue_limit_requested != -1)
			maybe_resize_buffer();

		// reset thread specific storage
		for (unsigned int i = 0; i < m_threads_storage.size(); i++)
			m_threads_storage[i]->swap_allocators();
	}

	int alert_manager::set_alert_queue_size_limit(int queue_size_limit_)
	{
		mutex::scoped_lock lock(m_mutex);
		shared_lock::scoped_lock wlock(m_shared_lock, shared_lock::exclusive);
		m_queue_limit_requested = queue_size_limit_;

		// try to resize the queue. If it can't be done now it
		// will be done the next time get_all() is called
		maybe_resize_buffer();

		return m_queue_limit_requested;
	}
}

