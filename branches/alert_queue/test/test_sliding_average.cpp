/*

Copyright (c) 2014, Arvid Norberg
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

#include "test.hpp"
#include "libtorrent/sliding_average.hpp"

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


int test_main()
{
	using namespace libtorrent;

	// make sure we react quickly for the first few samples
	{
		sliding_average<10> avg;

		avg.add_sample(-10);
		avg.add_sample(10);

		TEST_EQUAL(avg.mean(), 0);
	}
	{
		sliding_average<10> avg;

		avg.add_sample(10);
		avg.add_sample(20);

		TEST_EQUAL(avg.mean(), 15);
	}

	// make sure we converge
	{
		sliding_average<10> avg;
		avg.add_sample(100);
		for (int i = 0; i < 20; ++i)
			avg.add_sample(10);
		TEST_CHECK(abs(avg.mean() - 10) <= 3);
	}
	{
		sliding_average<10> avg;
		avg.add_sample(-100);
		for (int i = 0; i < 20; ++i)
			avg.add_sample(-10);
		TEST_CHECK(abs(avg.mean() + 10) <= 3);
	}

	// test with a more realistic input
	{
		sliding_average<10> avg;
		for (int i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i)
			avg.add_sample(samples[i]);
		TEST_CHECK(abs(avg.mean() - 60) <= 3);
	}
	return 0;
}

