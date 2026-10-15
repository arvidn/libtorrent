/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_TICK_THREAD_HPP_INCLUDED
#define TORRENT_DISK_TICK_THREAD_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/time.hpp" // for steady_clock

#include <exception>
#include <functional>
#include <mutex>
#include <vector>

#if !defined TORRENT_BUILD_SIMULATOR
#include <thread>
#endif

namespace libtorrent::aux {

// runs periodic maintenance for disk I/O back-ends on a dedicated thread, so
// the disk I/O threads only run jobs and maintenance happens even while a
// back-end is idle. Tasks run one at a time, with no lock held.
// Everything is best-effort. add() and schedule() report failure to queue a
// task rather than throw, an exception thrown by a task is swallowed and a
// periodic task that throws is not run again.
// The tasks are driven by a timer on an io_context. In the simulator that's
// the one passed in, so the tasks follow the simulated clock. Otherwise it's
// a private one, run by the dedicated thread
struct TORRENT_EXTRA_EXPORT disk_tick_thread
{
	// called with the current time, returns the next time it wants to run, or
	// time_point::max() to not run again
	using task = std::function<steady_clock::time_point(steady_clock::time_point now)>;

	explicit disk_tick_thread(io_context& ios);
	~disk_tick_thread();
	disk_tick_thread(disk_tick_thread const&) = delete;
	disk_tick_thread& operator=(disk_tick_thread const&) = delete;

	// the task runs for the first time right away. Returns false if it could
	// not be queued
	bool add(task t) noexcept;

	// runs f once, at the given time. Returns false if it could not be queued
	template <typename F>
	bool schedule(steady_clock::time_point const when, F f) noexcept
	{
		try
		{
			return add_at(when, task([fun = std::move(f)](steady_clock::time_point) mutable {
				fun();
				return steady_clock::time_point::max();
			}));
		}
		catch (std::exception const&)
		{
			return false;
		}
	}

	// tells the thread to exit without waiting for it, and drops all pending
	// tasks. Tasks added afterwards never run. A task that's running when
	// abort() is called completes
	void abort();

	// waits for the thread to exit. Implies abort(). Must not be called from
	// a task
	void join();

private:
	bool add_at(steady_clock::time_point when, task t) noexcept;

	// runs every task that is due. l must hold m_mutex, it's released while
	// each task runs
	void run_due(std::unique_lock<std::mutex>& l);

	// arms the timer for the earliest deadline. Requires m_mutex to be held,
	// which also serializes the calls on m_timer from different threads
	void arm() noexcept;
	void on_timer(error_code const& ec);

	struct entry
	{
		steady_clock::time_point due;
		task t;
	};

	// inverted, so the earliest deadline ends up at the front of the heap
	static bool heap_compare_due(entry const& lhs, entry const& rhs);

	std::mutex m_mutex;
	// min-heap ordered by due
	std::vector<entry> m_tasks;
	bool m_abort = false;

#if !defined TORRENT_BUILD_SIMULATOR
	io_context m_ios{1};
#endif
	deadline_timer m_timer;
	// whether a wait for the front task is pending. Cleared when the wait
	// expires or arming fails, so the next add_at() arms the timer again
	bool m_armed = false;

#if !defined TORRENT_BUILD_SIMULATOR
	executor_work_guard<io_context::executor_type> m_work;
	// declared last, the thread starts in the constructor
	std::thread m_thread;
#endif
};
}

#endif
