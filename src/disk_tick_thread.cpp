/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_tick_thread.hpp"
#include "libtorrent/aux_/platform_util.hpp" // for set_thread_name
#include "libtorrent/assert.hpp"

#include <algorithm>

namespace libtorrent::aux {

using namespace std::chrono_literals;

bool disk_tick_thread::heap_compare_due(entry const& lhs, entry const& rhs)
{
	return lhs.due > rhs.due;
}

#if defined TORRENT_BUILD_SIMULATOR

disk_tick_thread::disk_tick_thread(io_context& ios)
	: m_timer(ios)
{}

void disk_tick_thread::join() { abort(); }

#else

disk_tick_thread::disk_tick_thread(io_context&)
	: m_timer(m_ios)
	, m_work(make_work_guard(m_ios))
	, m_thread([this] {
		set_thread_name("lt-disk-tick");
		m_ios.run();
	})
{}

void disk_tick_thread::join()
{
	// cancelling the timer leaves nothing for run() to do once the work guard
	// is dropped
	abort();
	m_work.reset();
	// may be called more than once
	if (m_thread.joinable())
		m_thread.join();
}

#endif

disk_tick_thread::~disk_tick_thread() { join(); }

bool disk_tick_thread::add(task t) noexcept { return add_at(steady_clock::now(), std::move(t)); }

bool disk_tick_thread::add_at(steady_clock::time_point const when, task t) noexcept
{
	std::lock_guard<std::mutex> l(m_mutex);
	if (m_abort)
		return false;
	// the timer is set for the earliest deadline, so only a new earliest one
	// needs to move it
	bool const earliest = m_tasks.empty() || when < m_tasks.front().due;
	try
	{
		m_tasks.push_back({when, std::move(t)});
	}
	catch (std::exception const&)
	{
		return false;
	}
	std::push_heap(m_tasks.begin(), m_tasks.end(), heap_compare_due);
	if (earliest || !m_armed)
		arm();
	return true;
}

void disk_tick_thread::arm() noexcept
{
	try
	{
		// the simulator cannot fast-forward to a deadline that has passed
		m_timer.expires_after(
			std::max<steady_clock::duration>(m_tasks.front().due - steady_clock::now(), 1ms));
		m_timer.async_wait([this](error_code const& ec) { on_timer(ec); });
		m_armed = true;
	}
	catch (std::exception const&)
	{
		m_armed = false;
	}
}

void disk_tick_thread::on_timer(error_code const& ec)
{
	// the wait was cancelled, by arm() moving the deadline or by abort()
	if (ec)
		return;
	std::unique_lock<std::mutex> l(m_mutex);
	m_armed = false;
	run_due(l);
	// abort() leaves nothing in the heap
	if (!m_tasks.empty())
		arm();
}

void disk_tick_thread::run_due(std::unique_lock<std::mutex>& l)
{
	steady_clock::time_point const now = steady_clock::now();
	while (!m_tasks.empty() && m_tasks.front().due <= now)
	{
		std::pop_heap(m_tasks.begin(), m_tasks.end(), heap_compare_due);
		task t = std::move(m_tasks.back().t);
		m_tasks.pop_back();
		l.unlock();
		steady_clock::time_point next = steady_clock::time_point::max();
		try
		{
			next = t(now);
		}
		catch (...)
		{
			// nowhere to report it. The task is dropped
		}
		l.lock();
		// abort() may have dropped the tasks while this one ran
		if (next == steady_clock::time_point::max() || m_abort)
			continue;
		// a deadline that isn't in the future would make this loop spin
		TORRENT_ASSERT(next > now);
		next = std::max(next, now + 1ms);
		// the slot pop_back() freed is normally still there, so this only
		// allocates if add_at() grew the vector in the meantime
		try
		{
			m_tasks.push_back({next, std::move(t)});
		}
		catch (std::exception const&)
		{
			continue;
		}
		std::push_heap(m_tasks.begin(), m_tasks.end(), heap_compare_due);
	}
}

void disk_tick_thread::abort()
{
	// the tasks are destructed outside the lock in case one calls back into
	// this object
	std::vector<entry> tasks;
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_abort = true;
		tasks.swap(m_tasks);
		m_timer.cancel();
	}
}
}
