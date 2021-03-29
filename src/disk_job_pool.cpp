/*

Copyright (c) 2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_job_pool.hpp"
#include "libtorrent/aux_/disk_io_job.hpp"

namespace lt {
namespace aux {

	disk_job_pool::disk_job_pool()
		: m_jobs_in_use(0)
		, m_read_jobs(0)
		, m_write_jobs(0)
		, m_job_pool(sizeof(disk_io_job))
	{}

	disk_job_pool::~disk_job_pool()
	{
// #error this should be fixed!
//		TORRENT_ASSERT(m_jobs_in_use == 0);
	}

	disk_io_job* disk_job_pool::allocate_job(job_action_t const type)
	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		void* storage = m_job_pool.malloc();
		m_job_pool.set_next_size(100);
		++m_jobs_in_use;
		if (type == job_action_t::read) ++m_read_jobs;
		else if (type == job_action_t::write) ++m_write_jobs;
		l.unlock();
		TORRENT_ASSERT(storage);

		auto* ptr = new (storage) disk_io_job;
		ptr->action = type;
#if TORRENT_USE_ASSERTS
		ptr->in_use = true;
#endif
		return ptr;
	}

	void disk_job_pool::free_job(disk_io_job* j)
	{
		TORRENT_ASSERT(j);
		if (j == nullptr) return;
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(j->in_use);
		j->in_use = false;
#endif
		job_action_t const type = j->action;
		j->~disk_io_job();
		std::lock_guard<std::mutex> l(m_job_mutex);
		if (type == job_action_t::read) --m_read_jobs;
		else if (type == job_action_t::write) --m_write_jobs;
		--m_jobs_in_use;
		m_job_pool.free(j);
	}

	void disk_job_pool::free_jobs(disk_io_job** j, int const num)
	{
		if (num == 0) return;

		int read_jobs = 0;
		int write_jobs = 0;
		for (int i = 0; i < num; ++i)
		{
			job_action_t const type = j[i]->action;
			j[i]->~disk_io_job();
			if (type == job_action_t::read) ++read_jobs;
			else if (type == job_action_t::write) ++write_jobs;
		}

		std::lock_guard<std::mutex> l(m_job_mutex);
		m_read_jobs -= read_jobs;
		m_write_jobs -= write_jobs;
		m_jobs_in_use -= num;
		for (int i = 0; i < num; ++i)
			m_job_pool.free(j[i]);
	}
}
}
