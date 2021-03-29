/*

Copyright (c) 2016-2020, Arvid Norberg
Copyright (c) 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_JOB_FENCE_HPP_INCLUDE
#define TORRENT_DISK_JOB_FENCE_HPP_INCLUDE

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/tailqueue.hpp"

#include <atomic>
#include <mutex>

namespace lt {

struct counters;
}

namespace lt::aux {

	struct disk_io_job;

	// implements the disk I/O job fence used by the default_storage
	// to provide to the disk thread. Whenever a disk job needs
	// exclusive access to the storage for that torrent, it raises
	// the fence, blocking all new jobs, until there are no longer
	// any outstanding jobs on the torrent, then the fence is lowered
	// and it can be performed, along with the backlog of jobs that
	// accrued while the fence was up
	struct TORRENT_EXTRA_EXPORT disk_job_fence
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
		// storage, fence_post_fence is returned.
		// fence_post_none if the fence job was queued.
		enum { fence_post_fence = 0, fence_post_none = 1 };
		int raise_fence(disk_io_job*, counters&);
		bool has_fence() const;

		// called whenever a job completes and is posted back to the
		// main network thread. the tailqueue of jobs will have the
		// backed-up jobs prepended to it in case this resulted in the
		// fence being lowered.
		int job_complete(disk_io_job*, tailqueue<disk_io_job>&);
		int num_outstanding_jobs() const { return m_outstanding_jobs; }

		// if there is a fence up, returns true and adds the job
		// to the queue of blocked jobs
		bool is_blocked(disk_io_job*);

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


}

#endif
