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
#include <iostream>
#include <vector>
#include <utility>
#include <set>

#include "libtorrent/buffer.hpp"
#include "libtorrent/chained_buffer.hpp"
#include "libtorrent/socket.hpp"

#include "test.hpp"

using namespace libtorrent;

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

// -- test buffer --

TORRENT_TEST(buffer)
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

}

// -- test chained buffer --

std::set<char*> buffer_list;

void free_buffer(char* m, void* userdata, block_cache_reference ref)
{
	TEST_CHECK(userdata == (void*)0x1337);
	std::set<char*>::iterator i = buffer_list.find(m);
	TEST_CHECK(i != buffer_list.end());

	buffer_list.erase(i);
	std::free(m);
}

char* allocate_buffer(int size)
{
	char* mem = (char*)std::malloc(size);
	buffer_list.insert(mem);
	return mem;
}

template <class T>
int copy_buffers(T const& b, char* target)
{
	int copied = 0;
	for (typename T::const_iterator i = b.begin()
		, end(b.end()); i != end; ++i)
	{
		memcpy(target, boost::asio::buffer_cast<char const*>(*i), boost::asio::buffer_size(*i));
		target += boost::asio::buffer_size(*i);
		copied += boost::asio::buffer_size(*i);
	}
	return copied;
}

bool compare_chained_buffer(chained_buffer& b, char const* mem, int size)
{
	if (size == 0) return true;
	std::vector<char> flat(size);
	std::vector<boost::asio::const_buffer> const& iovec2 = b.build_iovec(size);
	int copied = copy_buffers(iovec2, &flat[0]);
	TEST_CHECK(copied == size);
	return std::memcmp(&flat[0], mem, size) == 0;
}

TORRENT_TEST(chained_buffer)
{
	char data[] = "foobar";
	{
		chained_buffer b;

		TEST_CHECK(b.empty());
		TEST_EQUAL(b.capacity(), 0);
		TEST_EQUAL(b.size(), 0);
		TEST_EQUAL(b.space_in_last_buffer(), 0);
		TEST_CHECK(buffer_list.empty());

		// there are no buffers, we should not be able to allocate
		// an appendix in an existing buffer
		TEST_EQUAL(b.allocate_appendix(1), 0);

		char* b1 = allocate_buffer(512);
		std::memcpy(b1, data, 6);
		b.append_buffer(b1, 512, 6, &free_buffer, (void*)0x1337);
		TEST_EQUAL(buffer_list.size(), 1);

		TEST_CHECK(b.capacity() == 512);
		TEST_CHECK(b.size() == 6);
		TEST_CHECK(!b.empty());
		TEST_CHECK(b.space_in_last_buffer() == 512 - 6);

		b.pop_front(3);

		TEST_CHECK(b.capacity() == 512);
		TEST_CHECK(b.size() == 3);
		TEST_CHECK(!b.empty());
		TEST_CHECK(b.space_in_last_buffer() == 512 - 6);

		bool ret = b.append(data, 6);

		TEST_CHECK(ret == true);
		TEST_CHECK(b.capacity() == 512);
		TEST_CHECK(b.size() == 9);
		TEST_CHECK(!b.empty());
		TEST_CHECK(b.space_in_last_buffer() == 512 - 12);

		char data2[1024];
		ret = b.append(data2, 1024);

		TEST_CHECK(ret == false);

		char* b2 = allocate_buffer(512);
		std::memcpy(b2, data, 6);
		b.append_buffer(b2, 512, 6, free_buffer, (void*)0x1337);
		TEST_CHECK(buffer_list.size() == 2);

		char* b3 = allocate_buffer(512);
		std::memcpy(b3, data, 6);
		b.append_buffer(b3, 512, 6, &free_buffer, (void*)0x1337);
		TEST_CHECK(buffer_list.size() == 3);

		TEST_CHECK(b.capacity() == 512 * 3);
		TEST_CHECK(b.size() == 21);
		TEST_CHECK(!b.empty());
		TEST_CHECK(b.space_in_last_buffer() == 512 - 6);

		TEST_CHECK(compare_chained_buffer(b, "barfoobar", 9));

		for (int i = 1; i < 21; ++i)
			TEST_CHECK(compare_chained_buffer(b, "barfoobarfoobarfoobar", i));

		b.pop_front(5 + 6);

		TEST_CHECK(buffer_list.size() == 2);
		TEST_CHECK(b.capacity() == 512 * 2);
		TEST_CHECK(b.size() == 10);
		TEST_CHECK(!b.empty());
		TEST_CHECK(b.space_in_last_buffer() == 512 - 6);

		char const* str = "obarfooba";
		TEST_CHECK(compare_chained_buffer(b, str, 9));

		for (int i = 0; i < 9; ++i)
		{
			b.pop_front(1);
			++str;
			TEST_CHECK(compare_chained_buffer(b, str, 8 - i));
			TEST_CHECK(b.size() == 9 - i);
		}

		char* b4 = allocate_buffer(20);
		std::memcpy(b4, data, 6);
		std::memcpy(b4 + 6, data, 6);
		b.append_buffer(b4, 20, 12, &free_buffer, (void*)0x1337);
		TEST_CHECK(b.space_in_last_buffer() == 8);

		ret = b.append(data, 6);
		TEST_CHECK(ret == true);
		TEST_CHECK(b.space_in_last_buffer() == 2);
		std::cout << b.space_in_last_buffer() << std::endl;
		ret = b.append(data, 2);
		TEST_CHECK(ret == true);
		TEST_CHECK(b.space_in_last_buffer() == 0);
		std::cout << b.space_in_last_buffer() << std::endl;

		char* b5 = allocate_buffer(20);
		std::memcpy(b4, data, 6);
		b.append_buffer(b5, 20, 6, &free_buffer, (void*)0x1337);

		b.pop_front(22);
		TEST_CHECK(b.size() == 5);
	}
	TEST_CHECK(buffer_list.empty());
}

