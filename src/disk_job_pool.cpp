/*

Copyright (c) 2014, 2016-2017, 2020-2022, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_job_pool.hpp"
#include "libtorrent/aux_/mmap_disk_job.hpp"
#include "libtorrent/aux_/pread_disk_job.hpp"

namespace libtorrent {
namespace aux {

	template <typename T>
	disk_job_pool<T>::disk_job_pool()
		: m_jobs_in_use(0)
		, m_read_jobs(0)
		, m_write_jobs(0)
		, m_job_pool()
	{}

	template <typename T>
	disk_job_pool<T>::~disk_job_pool()
	{
//		#error this should be fixed!
//		TORRENT_ASSERT(m_jobs_in_use == 0);
	}

	template <typename T>
	void disk_job_pool<T>::free_job(T* j)
	{
		TORRENT_ASSERT(j);
		if (j == nullptr) return;
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(j->in_use);
		j->in_use = false;
#endif
		job_action_t const type = j->get_type();
		std::lock_guard<std::mutex> l(m_job_mutex);
		if (type == job_action_t::read) --m_read_jobs;
		else if (type == job_action_t::write) --m_write_jobs;
		--m_jobs_in_use;
		m_job_pool.destroy(j);
	}

	template <typename T>
	void disk_job_pool<T>::free_jobs(T** j, int const num)
	{
		if (num == 0) return;

		int read_jobs = 0;
		int write_jobs = 0;
		for (int i = 0; i < num; ++i)
		{
			job_action_t const type = j[i]->get_type();
			if (type == job_action_t::read) ++read_jobs;
			else if (type == job_action_t::write) ++write_jobs;
		}

		std::lock_guard<std::mutex> l(m_job_mutex);
		m_read_jobs -= read_jobs;
		m_write_jobs -= write_jobs;
		m_jobs_in_use -= num;
		for (int i = 0; i < num; ++i)
			m_job_pool.destroy(j[i]);
	}

	template struct disk_job_pool<aux::mmap_disk_job>;
	template struct disk_job_pool<aux::pread_disk_job>;
}
}
