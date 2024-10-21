/*

Copyright (c) 2010, 2013-2017, 2020-2022, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_JOB_POOL
#define TORRENT_DISK_JOB_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/pool.hpp"
#include "libtorrent/aux_/disk_job.hpp"
#include <mutex>

namespace libtorrent {
namespace aux {

	template <typename T>
	struct disk_job_pool
	{
		disk_job_pool();
		~disk_job_pool();

		template <typename JobType, typename Storage, typename... Args>
		T* allocate_job(
			disk_job_flags_t const flags
			, Storage stor
			, Args&&... args)
		{
			std::lock_guard<std::mutex> l(m_job_mutex);
			auto* ptr = m_job_pool.malloc();
			new (ptr) T{
				disk_job{
				tailqueue_node<disk_job>{},
				flags,
				status_t{},
				storage_error{},
				JobType{std::forward<Args>(args)...},
#if TORRENT_USE_ASSERTS
				true, // in_use
				false, // job_posted
				false, // callback_called
				false, // blocked
#endif
				},
				std::move(stor)};
			m_job_pool.set_next_size(100);
			++m_jobs_in_use;
			if constexpr (std::is_same_v<JobType, job::read>)
				++m_read_jobs;
			else if constexpr (std::is_same_v<JobType, job::write>)
				++m_write_jobs;

			return ptr;
		}

		void free_job(T* j);
		void free_jobs(T** j, int num);

		int jobs_in_use() const { return m_jobs_in_use; }
		int read_jobs_in_use() const { return m_read_jobs; }
		int write_jobs_in_use() const { return m_write_jobs; }

	private:

		// total number of in-use jobs
		int m_jobs_in_use;
		// total number of in-use read jobs
		int m_read_jobs;
		// total number of in-use write jobs
		int m_write_jobs;

		std::mutex m_job_mutex;
		aux::object_pool<T> m_job_pool;
	};

	struct mmap_disk_job;
	struct pread_disk_job;
	extern template struct disk_job_pool<aux::mmap_disk_job>;
	extern template struct disk_job_pool<aux::pread_disk_job>;
}
}

#endif // TORRENT_DISK_JOB_POOL
