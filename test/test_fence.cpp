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

#include "libtorrent/storage.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/performance_counters.hpp"
#include "test.hpp"

#include <atomic>

using namespace lt;

using lt::aux::disk_job_fence;

TORRENT_TEST(empty_fence)
{
	disk_job_fence fence;
	counters cnt;

	disk_io_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
	int ret_int = 0;
	bool ret = false;
	// add a fence job
	ret_int = fence.raise_fence(&test_job[5], &test_job[6], cnt);
	// since we don't have any outstanding jobs
	// we need to post this job
	TEST_CHECK(ret_int == disk_job_fence::fence_post_fence);

	ret = fence.is_blocked(&test_job[7]);
	TEST_CHECK(ret == true);
	ret = fence.is_blocked(&test_job[8]);
	TEST_CHECK(ret == true);

	tailqueue<disk_io_job> jobs;

	// complete the fence job
	fence.job_complete(&test_job[5], jobs);

	// now it's fine to post the blocked jobs
	TEST_CHECK(jobs.size() == 2);
	TEST_CHECK(jobs.first() == &test_job[7]);

	// the disk_io_fence has an assert in its destructor
	// to make sure all outstanding jobs are completed, so we must
	// complete them before we're done
	fence.job_complete(&test_job[7], jobs);
	fence.job_complete(&test_job[8], jobs);
}

TORRENT_TEST(job_fence)
{
	counters cnt;
	disk_job_fence fence;

	disk_io_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
	int ret_int = 0;
	bool ret = false;
	TEST_CHECK(fence.num_outstanding_jobs() == 0);
	ret = fence.is_blocked(&test_job[0]);
	TEST_CHECK(ret == false);
	TEST_CHECK(fence.num_outstanding_jobs() == 1);
	ret = fence.is_blocked(&test_job[1]);
	TEST_CHECK(ret == false);
	ret = fence.is_blocked(&test_job[2]);
	TEST_CHECK(ret == false);
	ret = fence.is_blocked(&test_job[3]);
	TEST_CHECK(ret == false);
	ret = fence.is_blocked(&test_job[4]);
	TEST_CHECK(ret == false);

	TEST_CHECK(fence.num_outstanding_jobs() == 5);
	TEST_CHECK(fence.num_blocked() == 0);

	// add a fence job
	ret_int = fence.raise_fence(&test_job[5], &test_job[6], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret_int == disk_job_fence::fence_post_flush);

	ret = fence.is_blocked(&test_job[7]);
	TEST_CHECK(ret == true);
	ret = fence.is_blocked(&test_job[8]);
	TEST_CHECK(ret == true);

	tailqueue<disk_io_job> jobs;

	fence.job_complete(&test_job[3], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[2], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[4], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[1], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[0], jobs);
	TEST_EQUAL(jobs.size(), 0);

	// the flush job completes
	fence.job_complete(&test_job[6], jobs);

	// this was the last job. Now we should be
	// able to run the fence job
	TEST_EQUAL(jobs.size(), 1);

	TEST_CHECK(jobs.first() == &test_job[5]);
	jobs.pop_front();

	// complete the fence job
	fence.job_complete(&test_job[5], jobs);

	// now it's fine to post the blocked jobs
	TEST_EQUAL(jobs.size(), 2);
	TEST_CHECK(jobs.first() == &test_job[7]);

	// the disk_io_fence has an assert in its destructor
	// to make sure all outstanding jobs are completed, so we must
	// complete them before we're done
	fence.job_complete(&test_job[7], jobs);
	fence.job_complete(&test_job[8], jobs);
}

TORRENT_TEST(double_fence)
{
	counters cnt;
	disk_job_fence fence;

	disk_io_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
	int ret_int = 0;
	bool ret = false;
	TEST_CHECK(fence.num_outstanding_jobs() == 0);
	ret = fence.is_blocked(&test_job[0]);
	TEST_CHECK(ret == false);
	TEST_CHECK(fence.num_outstanding_jobs() == 1);
	ret = fence.is_blocked(&test_job[1]);
	TEST_CHECK(ret == false);
	ret = fence.is_blocked(&test_job[2]);
	TEST_CHECK(ret == false);
	ret = fence.is_blocked(&test_job[3]);
	TEST_CHECK(ret == false);
	ret = fence.is_blocked(&test_job[4]);
	TEST_CHECK(ret == false);

	TEST_CHECK(fence.num_outstanding_jobs() == 5);
	TEST_CHECK(fence.num_blocked() == 0);

	// add two fence jobs
	ret_int = fence.raise_fence(&test_job[5], &test_job[6], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret_int == disk_job_fence::fence_post_flush);

	ret_int = fence.raise_fence(&test_job[7], &test_job[8], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret_int == disk_job_fence::fence_post_none);
	std::printf("ret: %d\n", ret_int);

	ret = fence.is_blocked(&test_job[9]);
	TEST_CHECK(ret == true);

	tailqueue<disk_io_job> jobs;

	fence.job_complete(&test_job[3], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[2], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[4], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[1], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[0], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[6], jobs);
	// this was the last job. Now we should be
	// able to run the fence job
	TEST_CHECK(jobs.size() == 1);

	TEST_CHECK(jobs.first() == &test_job[5]);
	jobs.pop_front();

	// complete the fence job
	fence.job_complete(&test_job[5], jobs);

	// now it's fine to run the next fence job
	// first we get the flush job
	TEST_CHECK(jobs.size() == 1);
	TEST_CHECK(jobs.first() == &test_job[8]);
	jobs.pop_front();

	fence.job_complete(&test_job[8], jobs);

	// then the fence itself
	TEST_CHECK(jobs.size() == 1);
	TEST_CHECK(jobs.first() == &test_job[7]);
	jobs.pop_front();

	fence.job_complete(&test_job[7], jobs);

	// and now we can run the remaining blocked job
	TEST_CHECK(jobs.size() == 1);
	TEST_CHECK(jobs.first() == &test_job[9]);

	// the disk_io_fence has an assert in its destructor
	// to make sure all outstanding jobs are completed, so we must
	// complete them before we're done
	fence.job_complete(&test_job[9], jobs);
}

