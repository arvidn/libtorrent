/*

Copyright (c) 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/sliding_average.hpp"

namespace {

// normal distributed samples. mean=60 stddev=10
int samples[] =  {
49, 51, 60, 46, 65, 53, 76, 59, 57, 54, 56, 51, 45, 80, 53, 62,
69, 67, 66, 56, 56, 61, 52, 61, 61, 62, 59, 53, 48, 68, 47, 47,
63, 51, 53, 54, 46, 65, 64, 64, 45, 68, 64, 66, 53, 42, 57, 58,
57, 47, 55, 59, 64, 61, 37, 67, 55, 52, 60, 60, 44, 57, 50, 77,
56, 54, 49, 68, 66, 64, 47, 60, 46, 47, 81, 74, 65, 62, 44, 75,
65, 43, 58, 59, 53, 67, 49, 51, 33, 47, 49, 50, 54, 48, 55, 80,
67, 51, 66, 52, 48, 57, 30, 51, 72, 65, 78, 56, 74, 68, 49, 66,
63, 57, 61, 62, 64, 62, 61, 52, 67, 64, 59, 61, 69, 60, 54, 69 };

} // anonymous namespace

using namespace lt;

// make sure we react quickly for the first few samples
TORRENT_TEST(reaction_time)
{
	aux::sliding_average<int, 10> avg;

	avg.add_sample(-10);
	avg.add_sample(10);

	TEST_EQUAL(avg.mean(), 0);
}

TORRENT_TEST(reaction_time2)
{
	aux::sliding_average<int, 10> avg;

	avg.add_sample(10);
	avg.add_sample(20);

	TEST_EQUAL(avg.mean(), 15);
}

// make sure we converge
TORRENT_TEST(converge)
{
	aux::sliding_average<int, 10> avg;
	avg.add_sample(100);
	for (int i = 0; i < 20; ++i)
		avg.add_sample(10);
	TEST_CHECK(abs(avg.mean() - 10) <= 3);
}

TORRENT_TEST(converge2)
{
	aux::sliding_average<int, 10> avg;
	avg.add_sample(-100);
	for (int i = 0; i < 20; ++i)
		avg.add_sample(-10);
	TEST_CHECK(abs(avg.mean() + 10) <= 3);
}

// test with a more realistic input
TORRENT_TEST(random_converge)
{
	aux::sliding_average<int, 10> avg;
	for (int i = 0; i < int(sizeof(samples)/sizeof(samples[0])); ++i)
		avg.add_sample(samples[i]);
	TEST_CHECK(abs(avg.mean() - 60) <= 3);
}

TORRENT_TEST(sliding_average)
{
	aux::sliding_average<int, 4> avg;
	TEST_EQUAL(avg.mean(), 0);
	TEST_EQUAL(avg.avg_deviation(), 0);
	avg.add_sample(500);
	TEST_EQUAL(avg.mean(), 500);
	TEST_EQUAL(avg.avg_deviation(), 0);
	avg.add_sample(501);
	TEST_EQUAL(avg.avg_deviation(), 1);
	avg.add_sample(0);
	avg.add_sample(0);
	std::printf("avg: %d dev: %d\n", avg.mean(), avg.avg_deviation());
	TEST_CHECK(abs(avg.mean() - 250) < 50);
	TEST_CHECK(abs(avg.avg_deviation() - 250) < 80);
}
