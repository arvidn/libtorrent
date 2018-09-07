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
#include "libtorrent/packet_pool.hpp"

using lt::packet_buffer;
using lt::packet_ptr;
using lt::packet_pool;
using lt::packet;

namespace {

packet_ptr make_pkt(packet_pool& pool, int const val)
{
	packet_ptr ret = pool.acquire(20);
	*reinterpret_cast<std::uint8_t*>(ret->buf) = std::uint8_t(val);
	return ret;
}

int get_val(packet* pkt)
{
	TORRENT_ASSERT(pkt != nullptr);
	return *reinterpret_cast<std::uint8_t*>(pkt->buf);
}

} // anonymous namespace

// test packet_buffer
TORRENT_TEST(insert)
{
	packet_pool pool;
	packet_buffer pb;

	TEST_EQUAL(pb.capacity(), 0);
	TEST_EQUAL(pb.size(), 0);
	TEST_EQUAL(pb.span(), 0);

	pb.insert(123, make_pkt(pool, 123));
	TEST_CHECK(pb.at(123 + 16) == nullptr);

	TEST_EQUAL(get_val(pb.at(123)), 123);
	TEST_CHECK(pb.capacity() > 0);
	TEST_EQUAL(pb.size(), 1);
	TEST_EQUAL(pb.span(), 1);
	TEST_EQUAL(pb.cursor(), 123);

	pb.insert(125, make_pkt(pool, 125));

	TEST_EQUAL(get_val(pb.at(125)), 125);
	TEST_EQUAL(pb.size(), 2);
	TEST_EQUAL(pb.span(), 3);
	TEST_EQUAL(pb.cursor(), 123);

	pb.insert(500, make_pkt(pool, 4));
	TEST_EQUAL(pb.size(), 3);
	TEST_EQUAL(pb.span(), 501 - 123);
	TEST_EQUAL(pb.capacity(), 512);

	pb.insert(500, make_pkt(pool, 5));
	TEST_EQUAL(pb.size(), 3);
	TEST_EQUAL(get_val(pb.insert(500, make_pkt(pool, 4)).get()), 5);
	TEST_EQUAL(pb.size(), 3);

	TEST_EQUAL(get_val(pb.remove(123).get()), 123);
	TEST_EQUAL(pb.size(), 2);
	TEST_EQUAL(pb.span(), 501 - 125);
	TEST_EQUAL(pb.cursor(), 125);
	TEST_EQUAL(get_val(pb.remove(125).get()), 125);
	TEST_EQUAL(pb.size(), 1);
	TEST_EQUAL(pb.span(), 1);
	TEST_EQUAL(pb.cursor(), 500);

	TEST_EQUAL(get_val(pb.remove(500).get()), 4);
	TEST_EQUAL(pb.size(), 0);
	TEST_EQUAL(pb.span(), 0);

	for (int i = 0; i < 0xff; ++i)
	{
		int index = (i + 0xfff0) & 0xffff;
		pb.insert(packet_buffer::index_type(index), make_pkt(pool, index + 1));
		std::printf("insert: %u (mask: %x)\n", index, int(pb.capacity() - 1));
		TEST_EQUAL(pb.capacity(), 512);
		if (i >= 14)
		{
			index = (index - 14) & 0xffff;
			std::printf("remove: %u\n", index);
			TEST_EQUAL(get_val(pb.remove(packet_buffer::index_type(index)).get()), std::uint8_t(index + 1));
			TEST_EQUAL(pb.size(), 14);
		}
	}
}

TORRENT_TEST(wrap)
{
	// test wrapping the indices
	packet_pool pool;
	packet_buffer pb;

	TEST_EQUAL(pb.size(), 0);

	pb.insert(0xfffe, make_pkt(pool, 1));
	TEST_EQUAL(get_val(pb.at(0xfffe)), 1);

	pb.insert(2, make_pkt(pool, 2));
	TEST_EQUAL(get_val(pb.at(2)), 2);

	pb.remove(0xfffe);
	TEST_CHECK(pb.at(0xfffe) == nullptr);
	TEST_EQUAL(get_val(pb.at(2)), 2);
}

TORRENT_TEST(wrap2)
{
	// test wrapping the indices
	packet_pool pool;
	packet_buffer pb;

	TEST_EQUAL(pb.size(), 0);

	pb.insert(0xfff3, make_pkt(pool, 1));
	TEST_EQUAL(get_val(pb.at(0xfff3)), 1);

	auto const new_index = packet_buffer::index_type((0xfff3 + pb.capacity()) & 0xffff);
	pb.insert(new_index, make_pkt(pool, 2));
	TEST_EQUAL(get_val(pb.at(new_index)), 2);

	packet_ptr old = pb.remove(0xfff3);
	TEST_CHECK(get_val(old.get()) == 1);
	TEST_CHECK(pb.at(0xfff3) == nullptr);
	TEST_EQUAL(get_val(pb.at(new_index)), 2);
}

TORRENT_TEST(reverse_wrap)
{
	// test wrapping the indices backwards
	packet_pool pool;
	packet_buffer pb;

	TEST_EQUAL(pb.size(), 0);

	pb.insert(0xfff3, make_pkt(pool, 1));
	TEST_EQUAL(get_val(pb.at(0xfff3)), 1);

	auto const new_index = packet_buffer::index_type((0xfff3 + pb.capacity()) & 0xffff);
	pb.insert(new_index, make_pkt(pool, 2));
	TEST_EQUAL(get_val(pb.at(new_index)), 2);

	packet_ptr old = pb.remove(0xfff3);
	TEST_CHECK(get_val(old.get()) == 1);
	TEST_CHECK(pb.at(0xfff3) == nullptr);
	TEST_EQUAL(get_val(pb.at(new_index)), 2);

	pb.insert(0xffff, make_pkt(pool, 3));
}
