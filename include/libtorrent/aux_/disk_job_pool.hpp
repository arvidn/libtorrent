/*

Copyright (c) 2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_JOB_POOL
#define TORRENT_DISK_JOB_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/disk_io_job.hpp" // for job_action_t
#include <mutex>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/pool/pool.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {
namespace aux {

	struct disk_io_job;

	struct TORRENT_EXTRA_EXPORT disk_job_pool
	{
		disk_job_pool();
		~disk_job_pool();

		disk_io_job* allocate_job(job_action_t type);
		void free_job(disk_io_job* j);
		void free_jobs(disk_io_job** j, int num);

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
		boost::pool<> m_job_pool;
	};
}
}

#endif // TORRENT_DISK_JOB_POOL
