/*

Copyright (c) 2012, Arvid Norberg
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
#include "libtorrent/packet_buffer.hpp"

using libtorrent::packet_buffer;

// test packet_buffer
TORRENT_TEST(insert)
{
	packet_buffer<int> pb;

	int a123 = 123;
	int a125 = 125;
	int a500 = 500;
	int a501 = 501;

	TEST_EQUAL(pb.capacity(), 0);
	TEST_EQUAL(pb.size(), 0);
	TEST_EQUAL(pb.span(), 0);

	pb.insert(123, &a123);
	TEST_EQUAL(pb.at(123 + 16), 0);

	TEST_CHECK(pb.at(123) == &a123);
	TEST_CHECK(pb.capacity() > 0);
	TEST_EQUAL(pb.size(), 1);
	TEST_EQUAL(pb.span(), 1);
	TEST_EQUAL(pb.cursor(), 123);

	pb.insert(125, &a125);

	TEST_CHECK(pb.at(125) == &a125);
	TEST_EQUAL(pb.size(), 2);
	TEST_EQUAL(pb.span(), 3);
	TEST_EQUAL(pb.cursor(), 123);

	pb.insert(500, &a500);
	TEST_EQUAL(pb.size(), 3);
	TEST_EQUAL(pb.span(), 501 - 123);
	TEST_EQUAL(pb.capacity(), 512);

	pb.insert(500, &a501);
	TEST_EQUAL(pb.size(), 3);
	pb.insert(500, &a500);
	TEST_EQUAL(pb.size(), 3);

	TEST_CHECK(pb.remove(123) == &a123);
	TEST_EQUAL(pb.size(), 2);
	TEST_EQUAL(pb.span(), 501 - 125);
	TEST_EQUAL(pb.cursor(), 125);
	TEST_CHECK(pb.remove(125) == &a125);
	TEST_EQUAL(pb.size(), 1);
	TEST_EQUAL(pb.span(), 1);
	TEST_EQUAL(pb.cursor(), 500);

	TEST_CHECK(pb.remove(500) == &a500);
	TEST_EQUAL(pb.size(), 0);
	TEST_EQUAL(pb.span(), 0);

	for (int i = 0; i < 0xff; ++i)
	{
		int index = (i + 0xfff0) & 0xffff;
		pb.insert(index, reinterpret_cast<int*>(index + 1));
		fprintf(stderr, "insert: %u (mask: %x)\n", index, int(pb.capacity() - 1));
		TEST_EQUAL(pb.capacity(), 512);
		if (i >= 14)
		{
			index = (index - 14) & 0xffff;
			fprintf(stderr, "remove: %u\n", index);
			TEST_CHECK(pb.remove(index) == reinterpret_cast<int*>(index + 1));
			TEST_EQUAL(pb.size(), 14);
		}
	}
}

TORRENT_TEST(wrap)
{
	// test wrapping the indices
	packet_buffer<void> pb;

	TEST_EQUAL(pb.size(), 0);

	pb.insert(0xfffe, (void*)1);
	TEST_CHECK(pb.at(0xfffe) == (void*)1);

	pb.insert(2, (void*)2);
	TEST_CHECK(pb.at(2) == (void*)2);

	pb.remove(0xfffe);
	TEST_CHECK(pb.at(0xfffe) == (void*)0);
	TEST_CHECK(pb.at(2) == (void*)2);
}

TORRENT_TEST(wrap2)
{
	// test wrapping the indices
	packet_buffer<void> pb;

	TEST_EQUAL(pb.size(), 0);

	pb.insert(0xfff3, (void*)1);
	TEST_CHECK(pb.at(0xfff3) == (void*)1);

	int new_index = (0xfff3 + pb.capacity()) & 0xffff;
	pb.insert(new_index, (void*)2);
	TEST_CHECK(pb.at(new_index) == (void*)2);

	void* old = pb.remove(0xfff3);
	TEST_CHECK(old == (void*)1);
	TEST_CHECK(pb.at(0xfff3) == (void*)0);
	TEST_CHECK(pb.at(new_index) == (void*)2);
}

TORRENT_TEST(reverse_wrap)
{
	// test wrapping the indices backwards
	packet_buffer<void> pb;

	TEST_EQUAL(pb.size(), 0);

	pb.insert(0xfff3, (void*)1);
	TEST_CHECK(pb.at(0xfff3) == (void*)1);

	int new_index = (0xfff3 + pb.capacity()) & 0xffff;
	pb.insert(new_index, (void*)2);
	TEST_CHECK(pb.at(new_index) == (void*)2);

	void* old = pb.remove(0xfff3);
	TEST_CHECK(old == (void*)1);
	TEST_CHECK(pb.at(0xfff3) == (void*)0);
	TEST_CHECK(pb.at(new_index) == (void*)2);

	pb.insert(0xffff, (void*)0xffff);
}

