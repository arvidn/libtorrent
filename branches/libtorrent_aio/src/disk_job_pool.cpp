/*

Copyright (c) 2012-2013, Arvid Norberg
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

#include "libtorrent/disk_job_pool.hpp"
#include "libtorrent/disk_io_job.hpp"

namespace libtorrent
{
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

	disk_io_job* disk_job_pool::allocate_job(int type)
	{
		mutex::scoped_lock l(m_job_mutex);
		disk_io_job* ptr = (disk_io_job*)m_job_pool.malloc();
		m_job_pool.set_next_size(100);
		if (ptr == 0) return 0;
		++m_jobs_in_use;
		if (type == disk_io_job::read) ++m_read_jobs;
		else if (type == disk_io_job::write) ++m_write_jobs;
		l.unlock();
		TORRENT_ASSERT(ptr);

		new (ptr) disk_io_job;
		ptr->action = (disk_io_job::action_t)type;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		ptr->in_use = true;
#endif
		return ptr;
	}

	void disk_job_pool::free_job(disk_io_job* j)
	{
		TORRENT_ASSERT(j);
		if (j == 0) return;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		TORRENT_ASSERT(j->in_use);
		j->in_use = false;
#endif
		int type = j->action;
		j->~disk_io_job();
		mutex::scoped_lock l(m_job_mutex);
		if (type == disk_io_job::read) --m_read_jobs;
		else if (type == disk_io_job::write) --m_write_jobs;
		--m_jobs_in_use;
		m_job_pool.free(j);	
	}

	void disk_job_pool::free_jobs(disk_io_job** j, int num)
	{
		if (num == 0) return;

		int read_jobs = 0;
		int write_jobs = 0;
		for (int i = 0; i < num; ++i)
		{
			int type = j[i]->action;
			j[i]->~disk_io_job();
			if (type == disk_io_job::read) ++read_jobs;
			else if (type == disk_io_job::write) ++write_jobs;
		}
	
		mutex::scoped_lock l(m_job_mutex);
		m_read_jobs -= read_jobs;
		m_write_jobs -= write_jobs;
		m_jobs_in_use -= num;
		for (int i = 0; i < num; ++i)
			m_job_pool.free(j[i]);	
	}
}

