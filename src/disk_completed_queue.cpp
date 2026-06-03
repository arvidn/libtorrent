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
#if TORRENT_DISK_LATENCY_STATS
#include "libtorrent/time.hpp" // for clock_type, total_milliseconds
#include <cstdint>
#include <variant>
#include <algorithm>
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/container/static_vector.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent::aux {

void disk_completed_queue::abort_job(io_context& ioc, aux::disk_job* j)
{
	j->ret = disk_status::fatal_disk_error;
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

void disk_completed_queue::abort_jobs(io_context& ioc, jobqueue_t jobs)
{
	if (jobs.empty()) return;

	for (auto i = jobs.iterate(); i.get(); i.next())
	{
		auto* j = i.get();
		j->ret = disk_status::fatal_disk_error;
		j->error = storage_error(boost::asio::error::operation_aborted);
		j->flags |= aux::disk_job::aborted;
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(j->job_posted == false);
		j->job_posted = true;
#endif
	}
	std::lock_guard<std::mutex> l(m_completed_jobs_mutex);
	m_completed_jobs.append(std::move(jobs));

	if (!m_job_completions_in_flight && !m_completed_jobs.empty())
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

	if (!m_job_completions_in_flight && !m_completed_jobs.empty())
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

	boost::container::static_vector<aux::disk_job*, 64> to_delete;

	while (j)
	{
		TORRENT_ASSERT(j->job_posted == true);
		TORRENT_ASSERT(j->callback_called == false);
		DLOG("   callback: %s\n", print_job(*j).c_str());
		auto* next = static_cast<aux::disk_job*>(j->next);

#if TORRENT_DISK_LATENCY_STATS
		// record read-job latency just before delivering the result, so the
		// measured interval (queued on the network thread -> handler runs on
		// the network thread) covers the disk-thread queue, execution, and
		// the completion queue.
		if ((std::holds_alternative<aux::job::read>(j->action)
				|| std::holds_alternative<aux::job::partial_read>(j->action))
			&& !(j->flags & aux::disk_job::aborted))
		{
			std::int64_t const ms = total_milliseconds(clock_type::now() - j->start_time);
			constexpr std::int64_t max_bucket =
				counters::disk_read_latency20 - counters::disk_read_latency1;
			int const bucket =
				static_cast<int>(std::min(max_bucket, std::max(std::int64_t(0), ms / 30)));
			m_stats_counters.inc_stats_counter(counters::disk_read_latency1 + bucket);
		}
#endif

#if TORRENT_USE_ASSERTS
		j->callback_called = true;
#endif
		j->call_callback();
		to_delete.push_back(j);
		j = next;
		if (to_delete.size() == to_delete.capacity())
		{
			m_free_jobs(to_delete.data(), int(to_delete.size()));
			to_delete.clear();
		}
	}

	if (!to_delete.empty()) m_free_jobs(to_delete.data(), int(to_delete.size()));
}

}

