/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020, Alden Torres
Copyright (c) 2014-2017, 2019-2022, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/mmap_disk_job.hpp"
#include "libtorrent/aux_/disk_job_fence.hpp"
#include "libtorrent/performance_counters.hpp"
#include "test.hpp"

#include <atomic>

using namespace lt;

using lt::aux::disk_job_fence;
using lt::aux::mmap_disk_job;

TORRENT_TEST(empty_fence)
{
	disk_job_fence fence;
	counters cnt;

	mmap_disk_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
	int ret_int = 0;
	bool ret = false;
	// add a fence job
	ret_int = fence.raise_fence(&test_job[5], cnt);
	// since we don't have any outstanding jobs
	// we need to post this job
	TEST_CHECK(ret_int == disk_job_fence::fence_post_fence);

	ret = fence.is_blocked(&test_job[7]);
	TEST_CHECK(ret == true);
	ret = fence.is_blocked(&test_job[8]);
	TEST_CHECK(ret == true);

	aux::tailqueue<aux::mmap_disk_job> jobs;

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

	mmap_disk_job test_job[10];

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
	ret_int = fence.raise_fence(&test_job[5], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret_int == disk_job_fence::fence_post_none);

	ret = fence.is_blocked(&test_job[7]);
	TEST_CHECK(ret == true);
	ret = fence.is_blocked(&test_job[8]);
	TEST_CHECK(ret == true);

	aux::tailqueue<aux::mmap_disk_job> jobs;

	fence.job_complete(&test_job[3], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[2], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[4], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[1], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[0], jobs);

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

	mmap_disk_job test_job[10];

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
	ret_int = fence.raise_fence(&test_job[5], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret_int == disk_job_fence::fence_post_none);

	ret_int = fence.raise_fence(&test_job[7], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret_int == disk_job_fence::fence_post_none);
	std::printf("ret: %d\n", ret_int);

	ret = fence.is_blocked(&test_job[9]);
	TEST_CHECK(ret == true);

	aux::tailqueue<aux::mmap_disk_job> jobs;

	fence.job_complete(&test_job[3], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[2], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[4], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[1], jobs);
	TEST_CHECK(jobs.size() == 0);
	fence.job_complete(&test_job[0], jobs);

	// this was the last job. Now we should be
	// able to run the fence job
	TEST_CHECK(jobs.size() == 1);

	TEST_CHECK(jobs.first() == &test_job[5]);
	jobs.pop_front();

	// complete the fence job
	fence.job_complete(&test_job[5], jobs);

	// now it's fine to run the next fence job
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
