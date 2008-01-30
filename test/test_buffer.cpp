/*
	Copyright (c) 2003 - 2005, Arvid Norberg, Daniel Wallin
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions
	are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of Rasterbar Software nor the names of its
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

#include <cassert>
#include <boost/timer.hpp>
#include <iostream>
#include <vector>
#include <utility>

#include "libtorrent/buffer.hpp"

#include "test.hpp"

using libtorrent::buffer;
/*
template<class T>
T const& min_(T const& x, T const& y)
{
	return x < y ? x : y;
}

void test_speed()
{
	buffer b;

	char data[32];

	srand(0);

	boost::timer t;

	int const iterations = 5000000;
	int const step = iterations / 20;

	for (int i = 0; i < iterations; ++i)
	{
		int x = rand();

		if (i % step == 0) std::cerr << ".";

		std::size_t n = rand() % 32;
		n = 32;

		if (x % 2)
		{
			b.insert(data, data + n);
		}
		else
		{
			b.erase(min_(b.size(), n));
		}
	}

	float t1 = t.elapsed();
	std::cerr << "buffer elapsed: " << t.elapsed() << "\n";

	std::vector<char> v;

	srand(0);
	t.restart();

	for (int i = 0; i < iterations; ++i)
	{
		int x = rand();

		if (i % step == 0) std::cerr << ".";

		std::size_t n = rand() % 32;
		n = 32;

		if (x % 2)
		{
			v.insert(v.end(), data, data + n);
		}
		else
		{
			v.erase(v.begin(), v.begin() + min_(v.size(), n));
		}
	}

	float t2 = t.elapsed();
	std::cerr << "std::vector elapsed: " << t.elapsed() << "\n";

	assert(t1 < t2);
}
*/

int test_main()
{
	char data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

	buffer b;

	TEST_CHECK(b.size() == 0);
	TEST_CHECK(b.capacity() == 0);
	TEST_CHECK(b.empty());
	
	buffer::interval i = b.allocate(1);
	memcpy(i.begin, data, 1);

	TEST_CHECK(b.size() == 1);
	TEST_CHECK(b.capacity() >= 1);
	
	i = b.allocate(4);
	memcpy(i.begin, data + 1, 4);
	TEST_CHECK(b.size() == 5);
	TEST_CHECK(b.capacity() >= 5);

	i = b.allocate(4);
	memcpy(i.begin, data + 5, 4);
	TEST_CHECK(b.size() == 9);
	TEST_CHECK(b.capacity() >= 9);

	TEST_CHECK(!b.empty());

	buffer::interval_type read_data = b.data();
	TEST_CHECK(std::equal(read_data.first.begin, read_data.first.end, data));

	b.erase(5);

	TEST_CHECK(b.space_left() == 5);

	i = b.allocate(3);
	memcpy(i.begin, data, 3);
	TEST_CHECK(b.space_left() == 2);
	TEST_CHECK(b.size() == 7);

	read_data = b.data();
	TEST_CHECK(std::equal(read_data.first.begin, read_data.first.end, data + 5));
	TEST_CHECK(std::equal(read_data.second.begin, read_data.second.end, data));

	b.erase(7);
	
	TEST_CHECK(b.empty());
	return 0;
}

