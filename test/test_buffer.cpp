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

using namespace lt;

// -- test buffer --

static char const data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

TORRENT_TEST(buffer_constructor)
{

	{
		buffer b;
		TEST_CHECK(b.size() == 0);
		TEST_CHECK(b.empty());
	}

	{
		buffer b(10);
		TEST_CHECK(b.size() >= 10);
	}

	{
		buffer b(50, data);
		TEST_CHECK(std::memcmp(b.data(), data, 10) == 0);
		TEST_CHECK(b.size() >= 50);
	}
}

TORRENT_TEST(buffer_swap)
{
	buffer b1;
	TEST_CHECK(b1.size() == 0);
	buffer b2(10, data);
	auto const b2_size = b2.size();
	TEST_CHECK(b2_size >= 10);

	b1.swap(b2);

	TEST_CHECK(b2.size() == 0);
	TEST_CHECK(b1.size() == b2_size);
	TEST_CHECK(std::memcmp(b1.data(), data, 10) == 0);
}

TORRENT_TEST(buffer_subscript)
{
	buffer b(50, data);
	TEST_CHECK(std::memcmp(b.data(), data, 10) == 0);
	TEST_CHECK(b.size() >= 50);

	for (int i = 0; i < int(sizeof(data)/sizeof(data[0])); ++i)
		TEST_CHECK(b[i] == data[i]);
}

TORRENT_TEST(buffer_subscript2)
{
	buffer b(1);
	TEST_CHECK(b.size() >= 1);

	for (int i = 0; i < int(b.size()); ++i)
		b[i] = char(i & 0xff);

	for (int i = 0; i < int(b.size()); ++i)
		TEST_CHECK(b[i] == (i & 0xff));
}

TORRENT_TEST(buffer_move_construct)
{
	buffer b1(50, data);
	TEST_CHECK(std::memcmp(b1.data(), data, 10) == 0);
	TEST_CHECK(b1.size() >= 50);

	buffer b2(std::move(b1));

	TEST_CHECK(b1.empty());

	TEST_CHECK(std::memcmp(b2.data(), data, 10) == 0);
	TEST_CHECK(b2.size() >= 50);
}

TORRENT_TEST(buffer_move_assign)
{
	buffer b1(50, data);
	TEST_CHECK(std::memcmp(b1.data(), data, 10) == 0);
	TEST_CHECK(b1.size() >= 50);

	buffer b2;
	TEST_CHECK(b2.size() == 0);

	b2 = std::move(b1);

	TEST_CHECK(b1.size() == 0);

	TEST_CHECK(std::memcmp(b2.data(), data, 10) == 0);
	TEST_CHECK(b2.size() >= 50);
}

namespace {
// -- test chained buffer --

std::set<char*> buffer_list;

void free_buffer(char* m)
{
	auto const i = buffer_list.find(m);
	TEST_CHECK(i != buffer_list.end());

	buffer_list.erase(i);
	std::free(m);
}

char* allocate_buffer(int size)
{
	char* mem = static_cast<char*>(std::malloc(std::size_t(size)));
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
		copied += int(boost::asio::buffer_size(*i));
	}
	return copied;
}

bool compare_chained_buffer(chained_buffer& b, char const* mem, int size)
{
	if (size == 0) return true;
	std::vector<char> flat((std::size_t(size)));
	auto const iovec2 = b.build_iovec(size);
	int copied = copy_buffers(iovec2, &flat[0]);
	TEST_CHECK(copied == size);
	return std::memcmp(&flat[0], mem, std::size_t(size)) == 0;
}

struct holder
{
	holder(char* buf, std::size_t size) : m_buf(buf), m_size(size) {}
	~holder() { if (m_buf) free_buffer(m_buf); }
	holder(holder const&) = delete;
	holder& operator=(holder const&) = delete;
	holder(holder&& rhs) noexcept : m_buf(rhs.m_buf), m_size(rhs.m_size) { rhs.m_buf = nullptr; }
	holder& operator=(holder&& rhs) = delete;
	char* data() const { return m_buf; }
	std::size_t size() const { return m_size; }
private:
	char* m_buf;
	std::size_t m_size;
};

} // anonymous namespace

TORRENT_TEST(chained_buffer)
{
	char data_test[] = "foobar";
	{
		chained_buffer b;

		TEST_CHECK(b.empty());
		TEST_EQUAL(b.capacity(), 0);
		TEST_EQUAL(b.size(), 0);
		TEST_EQUAL(b.space_in_last_buffer(), 0);
		TEST_CHECK(buffer_list.empty());

		// there are no buffers, we should not be able to allocate
		// an appendix in an existing buffer
		TEST_EQUAL(b.allocate_appendix(1), static_cast<char*>(nullptr));

		char* b1 = allocate_buffer(512);
		std::memcpy(b1, data_test, 6);
		b.append_buffer(holder(b1, 512), 6);
		TEST_EQUAL(buffer_list.size(), 1);

		TEST_EQUAL(b.capacity(), 512);
		TEST_EQUAL(b.size(), 6);
		TEST_CHECK(!b.empty());
		TEST_EQUAL(b.space_in_last_buffer(), 512 - 6);

		b.pop_front(3);

		TEST_EQUAL(b.capacity(), 512 - 3);
		TEST_EQUAL(b.size(), 3);
		TEST_CHECK(!b.empty());
		TEST_EQUAL(b.space_in_last_buffer(), 512 - 6);

		bool ret = b.append({data_test, 6}) != nullptr;

		TEST_CHECK(ret == true);
		TEST_EQUAL(b.capacity(), 512 - 3);
		TEST_EQUAL(b.size(), 9);
		TEST_CHECK(!b.empty());
		TEST_EQUAL(b.space_in_last_buffer(), 512 - 12);

		char data2[1024];
		ret = b.append(data2) != nullptr;

		TEST_CHECK(ret == false);

		char* b2 = allocate_buffer(512);
		std::memcpy(b2, data_test, 6);
		b.append_buffer(holder(b2, 512), 6);
		TEST_EQUAL(buffer_list.size(), 2);

		char* b3 = allocate_buffer(512);
		std::memcpy(b3, data_test, 6);
		b.append_buffer(holder(b3, 512), 6);
		TEST_EQUAL(buffer_list.size(), 3);

		TEST_EQUAL(b.capacity(), 512 * 3 - 3);
		TEST_EQUAL(b.size(), 21);
		TEST_CHECK(!b.empty());
		TEST_EQUAL(b.space_in_last_buffer(), 512 - 6);

		TEST_CHECK(compare_chained_buffer(b, "barfoobar", 9));

		for (int i = 1; i < 21; ++i)
			TEST_CHECK(compare_chained_buffer(b, "barfoobarfoobarfoobar", i));

		b.pop_front(5 + 6);

		TEST_EQUAL(buffer_list.size(), 2);
		TEST_EQUAL(b.capacity(), 512 * 2 - 2);
		TEST_EQUAL(b.size(), 10);
		TEST_CHECK(!b.empty());
		TEST_EQUAL(b.space_in_last_buffer(), 512 - 6);

		char const* str = "obarfooba";
		TEST_CHECK(compare_chained_buffer(b, str, 9));

		for (int i = 0; i < 9; ++i)
		{
			b.pop_front(1);
			++str;
			TEST_CHECK(compare_chained_buffer(b, str, 8 - i));
			TEST_EQUAL(b.size(), 9 - i);
		}

		char* b4 = allocate_buffer(20);
		std::memcpy(b4, data_test, 6);
		std::memcpy(b4 + 6, data_test, 6);
		b.append_buffer(holder(b4, 20), 12);
		TEST_EQUAL(b.space_in_last_buffer(), 8);

		ret = b.append({data_test, 6}) != nullptr;
		TEST_CHECK(ret == true);
		TEST_EQUAL(b.space_in_last_buffer(), 2);
		std::cout << b.space_in_last_buffer() << std::endl;
		ret = b.append({data_test, 2}) != nullptr;
		TEST_CHECK(ret == true);
		TEST_EQUAL(b.space_in_last_buffer(), 0);
		std::cout << b.space_in_last_buffer() << std::endl;

		char* b5 = allocate_buffer(20);
		std::memcpy(b5, data_test, 6);
		b.append_buffer(holder(b5, 20), 6);

		b.pop_front(22);
		TEST_EQUAL(b.size(), 5);
	}
	TEST_CHECK(buffer_list.empty());
}
