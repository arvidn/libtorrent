/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifndef TORRENT_DISK_JOB_FENCE_HPP_INCLUDE
#define TORRENT_DISK_JOB_FENCE_HPP_INCLUDE

#include "libtorrent/config.hpp"
#include "libtorrent/tailqueue.hpp"

#include <atomic>
#include <mutex>

namespace libtorrent {

struct disk_io_job;
struct counters;

namespace aux {

	// implements the disk I/O job fence used by the storage_interface
	// to provide to the disk thread. Whenever a disk job needs
	// exclusive access to the storage for that torrent, it raises
	// the fence, blocking all new jobs, until there are no longer
	// any outstanding jobs on the torrent, then the fence is lowered
	// and it can be performed, along with the backlog of jobs that
	// accrued while the fence was up
	struct TORRENT_EXPORT disk_job_fence
	{
		disk_job_fence() = default;

#if TORRENT_USE_ASSERTS
		~disk_job_fence()
		{
			TORRENT_ASSERT(int(m_outstanding_jobs) == 0);
			TORRENT_ASSERT(m_blocked_jobs.size() == 0);
		}
#endif

		// returns one of the fence_* enums.
		// if there are no outstanding jobs on the
		// storage, fence_post_fence is returned, the flush job is expected
		// to be discarded by the caller.
		// fence_post_flush is returned if the fence job was blocked and queued,
		// but the flush job should be posted (i.e. put on the job queue)
		// fence_post_none if both the fence and the flush jobs were queued.
		enum { fence_post_fence = 0, fence_post_flush = 1, fence_post_none = 2 };
		int raise_fence(disk_io_job* fence_job, disk_io_job* flush_job
			, counters& cnt);
		bool has_fence() const;

		// called whenever a job completes and is posted back to the
		// main network thread. the tailqueue of jobs will have the
		// backed-up jobs prepended to it in case this resulted in the
		// fence being lowered.
		int job_complete(disk_io_job* j, tailqueue<disk_io_job>& job_queue);
		int num_outstanding_jobs() const { return m_outstanding_jobs; }

		// if there is a fence up, returns true and adds the job
		// to the queue of blocked jobs
		bool is_blocked(disk_io_job* j);

		// the number of blocked jobs
		int num_blocked() const;

	private:
		// when > 0, this storage is blocked for new async
		// operations until all outstanding jobs have completed.
		// at that point, the m_blocked_jobs are issued
		// the count is the number of fence job currently in the queue
		int m_has_fence = 0;

		// when there's a fence up, jobs are queued up in here
		// until the fence is lowered
		tailqueue<disk_io_job> m_blocked_jobs;

		// the number of disk_io_job objects there are, belonging
		// to this torrent, currently pending, hanging off of
		// cached_piece_entry objects. This is used to determine
		// when the fence can be lowered
		std::atomic<int> m_outstanding_jobs{0};

		// must be held when accessing m_has_fence and
		// m_blocked_jobs
		mutable std::mutex m_mutex;
	};


}}

#endif

