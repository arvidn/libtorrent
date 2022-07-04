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
#include "libtorrent/aux_/mmap_disk_job.hpp" // for job_action_t
#include "libtorrent/aux_/pool.hpp"
#include <mutex>

namespace libtorrent {
namespace aux {

	struct mmap_disk_job;

	struct TORRENT_EXTRA_EXPORT disk_job_pool
	{
		disk_job_pool();
		~disk_job_pool();

		template <typename JobType, typename... Args>
		mmap_disk_job* allocate_job(
			disk_job_flags_t const flags
			, std::shared_ptr<mmap_storage> storage
			, Args&&... args)
		{
			void* buf;
			{
				std::lock_guard<std::mutex> l(m_job_mutex);
				buf = m_job_pool.malloc();
				m_job_pool.set_next_size(100);
				++m_jobs_in_use;
				if constexpr (std::is_same_v<JobType, job::read>)
					++m_read_jobs;
				else if constexpr (std::is_same_v<JobType, job::write>)
					++m_write_jobs;
			}
			TORRENT_ASSERT(buf);

			auto* ptr = new (buf) mmap_disk_job{
				{
				tailqueue_node<disk_job>{},
				flags,
				status_t::no_error,
				storage_error{},
				JobType{std::forward<Args>(args)...},
#if TORRENT_USE_ASSERTS
				true, // in_use
				false, // job_posted
				false, // callback_called
				false, // blocked
#endif
				},
				std::move(storage),
			};

			return ptr;
		}

		void free_job(mmap_disk_job* j);
		void free_jobs(mmap_disk_job** j, int num);

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
		aux::pool m_job_pool;
	};
}
}

#endif // TORRENT_DISK_JOB_POOL
