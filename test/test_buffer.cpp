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
	
	b.resize(10);
	TEST_CHECK(b.size() == 10);
	TEST_CHECK(b.capacity() == 10);
	
	std::memcpy(b.begin(), data, 10);
	b.reserve(50);
	TEST_CHECK(std::memcmp(b.begin(), data, 10) == 0);
	TEST_CHECK(b.capacity() == 50);
	
	b.erase(b.begin() + 6, b.end());
	TEST_CHECK(std::memcmp(b.begin(), data, 6) == 0);
	TEST_CHECK(b.capacity() == 50);
	TEST_CHECK(b.size() == 6);

	b.insert(b.begin(), data + 5, data + 10);
	TEST_CHECK(b.capacity() == 50);
	TEST_CHECK(b.size() == 11);
	TEST_CHECK(std::memcmp(b.begin(), data + 5, 5) == 0);

	b.clear();
	TEST_CHECK(b.size() == 0);
	TEST_CHECK(b.capacity() == 50);

	b.insert(b.end(), data, data + 10);
	TEST_CHECK(b.size() == 10);
	TEST_CHECK(std::memcmp(b.begin(), data, 10) == 0);
	
	b.erase(b.begin(), b.end());
	TEST_CHECK(b.capacity() == 50);
	TEST_CHECK(b.size() == 0);

	buffer().swap(b);
	TEST_CHECK(b.capacity() == 0);

	return 0;
}

