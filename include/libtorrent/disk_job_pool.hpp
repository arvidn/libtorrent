/*

Copyright (c) 2010-2018, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_DISK_JOB_POOL
#define TORRENT_DISK_JOB_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/disk_io_job.hpp" // for job_action_t
#include <mutex>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/pool/pool.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

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

#endif // TORRENT_DISK_JOB_POOL
