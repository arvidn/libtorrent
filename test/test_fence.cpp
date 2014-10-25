#include "libtorrent/storage.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "test.hpp"

#include <boost/atomic.hpp>

using namespace libtorrent;

void test_disk_job_empty_fence()
{
	libtorrent::disk_job_fence fence;
	counters cnt;

	disk_io_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
	int ret = 0;
	// add a fence job
	ret = fence.raise_fence(&test_job[5], &test_job[6], cnt);
	// since we don't have any outstanding jobs
	// we need to post this job
	TEST_CHECK(ret == disk_job_fence::fence_post_fence);

	ret = fence.is_blocked(&test_job[7]);
	TEST_CHECK(ret == true);
	ret = fence.is_blocked(&test_job[8]);
	TEST_CHECK(ret == true);

	tailqueue jobs;

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

void test_disk_job_fence()
{
	counters cnt;
	libtorrent::disk_job_fence fence;

	disk_io_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
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
	ret = fence.raise_fence(&test_job[5], &test_job[6], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret == disk_job_fence::fence_post_flush);

	ret = fence.is_blocked(&test_job[7]);
	TEST_CHECK(ret == true);
	ret = fence.is_blocked(&test_job[8]);
	TEST_CHECK(ret == true);

	tailqueue jobs;

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

void test_disk_job_double_fence()
{
	counters cnt;
	libtorrent::disk_job_fence fence;

	disk_io_job test_job[10];

	// issue 5 jobs. None of them should be blocked by a fence
	int ret = 0;
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
	ret = fence.raise_fence(&test_job[5], &test_job[6], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret == disk_job_fence::fence_post_flush);

	ret = fence.raise_fence(&test_job[7], &test_job[8], cnt);
	// since we have outstanding jobs, no need
	// to post anything
	TEST_CHECK(ret == disk_job_fence::fence_post_none);
	fprintf(stderr, "ret: %d\n", ret);

	ret = fence.is_blocked(&test_job[9]);
	TEST_CHECK(ret == true);

	tailqueue jobs;

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

int test_main()
{
	test_disk_job_fence();
	test_disk_job_double_fence();
	test_disk_job_empty_fence();

	return 0;
}
