/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include "libtorrent/io_context.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/disk_completed_queue.hpp"
#include "libtorrent/aux_/disk_job.hpp"
#include "libtorrent/aux_/debug_disk_thread.hpp"
#include "libtorrent/aux_/array.hpp"

namespace libtorrent::aux {

void disk_completed_queue::abort_job(io_context& ioc, aux::disk_job* j)
{
	j->ret = status_t::fatal_disk_error;
	j->error = storage_error(boost::asio::error::operation_aborted);
	j->flags |= aux::disk_job::aborted;
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(j->job_posted == false);
	j->job_posted = true;
#endif
	std::lock_guard<std::mutex> l(m_completed_jobs_mutex);
	m_completed_jobs.push_back(j);

	if (!m_job_completions_in_flight)
	{
		DLOG("posting job handlers (%d)\n", m_completed_jobs.size());

		post(ioc, [this] { this->call_job_handlers(); });
		m_job_completions_in_flight = true;
	}
}

void disk_completed_queue::append(io_context& ioc, jobqueue_t jobs)
{
	std::lock_guard<std::mutex> l(m_completed_jobs_mutex);
	m_completed_jobs.append(std::move(jobs));

	if (!m_job_completions_in_flight)
	{
		DLOG("posting job handlers (%d)\n", m_completed_jobs.size());

		post(ioc, [this] { this->call_job_handlers(); });
		m_job_completions_in_flight = true;
	}
}

// This is run in the network thread
void disk_completed_queue::call_job_handlers()
{
	m_stats_counters.inc_stats_counter(counters::on_disk_counter);
	std::unique_lock<std::mutex> l(m_completed_jobs_mutex);

	DLOG("call_job_handlers (%d)\n", m_completed_jobs.size());

	TORRENT_ASSERT(m_job_completions_in_flight);
	m_job_completions_in_flight = false;

	auto* j = static_cast<aux::disk_job*>(m_completed_jobs.get_all());
	l.unlock();

	// TODO: use boost static_vector
	aux::array<aux::disk_job*, 64> to_delete;
	int cnt = 0;

	while (j)
	{
		TORRENT_ASSERT(j->job_posted == true);
		TORRENT_ASSERT(j->callback_called == false);
		DLOG("   callback: %s\n", print_job(*j).c_str());
		auto* next = static_cast<aux::disk_job*>(j->next);

#if TORRENT_USE_ASSERTS
		j->callback_called = true;
#endif
		j->call_callback();
		to_delete[cnt++] = j;
		j = next;
		if (cnt == int(to_delete.size()))
		{
			cnt = 0;
			m_free_jobs(to_delete.data(), int(to_delete.size()));
		}
	}

	if (cnt > 0) m_free_jobs(to_delete.data(), cnt);
}

}

