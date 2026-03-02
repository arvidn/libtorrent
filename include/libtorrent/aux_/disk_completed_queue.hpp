/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_COMPLETED_QUEUE_HPP
#define TORRENT_COMPLETED_QUEUE_HPP

#include <functional>
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp" // for jobqueue_t

namespace libtorrent::aux {

struct disk_job;

struct disk_completed_queue
{
	disk_completed_queue(std::function<void(disk_job**, int)> free_jobs, counters& cnt)
		: m_free_jobs(free_jobs)
		, m_stats_counters(cnt)
	{}

	void abort_job(io_context& ioc, aux::disk_job* j);
	void abort_jobs(io_context& ioc, jobqueue_t jobs);
	void append(io_context& ioc, jobqueue_t jobs);

private:

	// This is run in the network thread
	void call_job_handlers();

	// jobs that are completed are put on this queue
	// whenever the queue size grows from 0 to 1
	// a message is posted to the network thread, which
	// will then drain the queue and execute the jobs'
	// handler functions
	std::mutex m_completed_jobs_mutex;
	jobqueue_t m_completed_jobs;

	std::function<void(disk_job**, int)> m_free_jobs;

	counters& m_stats_counters;

	// this is protected by the completed_jobs_mutex. It's true whenever
	// there's a call_job_handlers message in-flight to the network thread. We
	// only ever keep one such message in flight at a time, and coalesce
	// completion callbacks in m_completed jobs
	bool m_job_completions_in_flight = false;
};

}

#endif
