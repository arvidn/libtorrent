/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2016-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "test.hpp"
#include "libtorrent/aux_/receive_buffer.hpp"

using namespace lt;
using lt::aux::receive_buffer;

TORRENT_TEST(recv_buffer_init)
{
	receive_buffer b;

	b.cut(0, 10);

	TEST_EQUAL(b.packet_size(), 10);
	TEST_EQUAL(b.packet_bytes_remaining(), 10);
	TEST_EQUAL(b.packet_finished(), false);
	TEST_EQUAL(b.pos(), 0);
	TEST_EQUAL(b.capacity(), 0);
}

TORRENT_TEST(recv_buffer_pos_at_end_false)
{
	receive_buffer b;

	b.cut(0, 1000);
	// allocate some space to receive into
	b.reserve(1000);

	b.received(1000);
	b.advance_pos(999);

	TEST_EQUAL(b.pos_at_end(), false);
}

TORRENT_TEST(recv_buffer_pos_at_end_true)
{
	receive_buffer b;
	b.cut(0, 1000);
	b.reserve(1000);
	b.reserve(1000);
	b.received(1000);
	b.advance_pos(1000);
	TEST_EQUAL(b.pos_at_end(), true);
}

TORRENT_TEST(recv_buffer_packet_finished)
{
	receive_buffer b;
	// packet_size = 10
	b.cut(0, 10);
	b.reserve(1000);
	b.reserve(1000);
	b.received(1000);

	for (int i = 0; i < 10; ++i)
	{
		TEST_EQUAL(b.packet_finished(), false);
		b.advance_pos(1);
	}
	TEST_EQUAL(b.packet_finished(), true);
}

TORRENT_TEST(recv_buffer_grow_floor)
{
	receive_buffer b;
	b.reset(1337);
	b.grow(100000);

	// the exact size depends on the OS allocator. Technically there's no upper
	// bound, but it's likely withint some reasonable size
	TEST_CHECK(b.capacity() >= 1337);
	TEST_CHECK(b.capacity() < 1337 + 1000);
}

TORRENT_TEST(recv_buffer_grow)
{
	receive_buffer b;
	b.reserve(200);
	b.grow(100000);
	// grow by 50%
	TEST_CHECK(b.capacity() >= 300);
	TEST_CHECK(b.capacity() < 300 + 500);
}

TORRENT_TEST(recv_buffer_grow_limit)
{
	receive_buffer b;
	b.reserve(2000);
	b.grow(2100);
	// grow by 50%, but capped by 2100 bytes
	TEST_CHECK(b.capacity() >= 2100);
	TEST_CHECK(b.capacity() < 2100 + 500);
	printf("capacity: %d\n", b.capacity());
}

TORRENT_TEST(recv_buffer_reserve_minimum_grow)
{
	receive_buffer b;
	b.reset(1337);
	b.reserve(20);

	// we only asked for 20 more bytes, but since the message size was set to
	// 1337, that's the minimum size to grow to
	TEST_CHECK(b.capacity() >= 1337);
	TEST_CHECK(b.capacity() < 1337 + 1000);
}

TORRENT_TEST(recv_buffer_reserve_grow)
{
	receive_buffer b;
	b.reserve(20);

	TEST_CHECK(b.capacity() >= 20);
	TEST_CHECK(b.capacity() < 20 + 500);
}

TORRENT_TEST(recv_buffer_reserve)
{
	receive_buffer b;
	auto range1 = b.reserve(100);

	int const capacity = b.capacity();

	b.reset(20);
	b.received(20);

	TEST_EQUAL(b.capacity(), capacity);

	auto range2 = b.reserve(50);

	TEST_EQUAL(b.capacity(), capacity);
	TEST_EQUAL(range1.begin() + 20, range2.begin());
	TEST_CHECK(range1.size() >= 20);
	TEST_CHECK(range2.size() >= 50);
}

TORRENT_TEST(receive_buffer_normalize)
{
	receive_buffer b;
	b.reset(16000);

	// receive one large packet, to allocate a large receive buffer
	for (int i = 0; i < 16; ++i)
	{
		b.reserve(1000);
		b.received(1000);
		b.normalize();
	}

	TEST_CHECK(b.capacity() >= 16000);
	int const start_capacity = b.capacity();

	// then receive lots of small packets. We should eventually re-allocate down
	// to a smaller buffer
	for (int i = 0; i < 15; ++i)
	{
		b.reset(160);
		b.reserve(160);
		b.received(160);
		b.normalize();
		printf("capacity: %d watermark: %d\n", b.capacity(), b.watermark());
	}

	TEST_CHECK(b.capacity() <= start_capacity / 2);
	printf("capacity: %d\n", b.capacity());
}

TORRENT_TEST(receive_buffer_max_receive)
{
	receive_buffer b;
	b.reset(2000);
	b.reserve(2000);
	b.received(2000);
	b.normalize();

	b.reset(20);
	int const max_receive = b.max_receive();
	TEST_CHECK(max_receive >= 2000);
	b.received(20);
	TEST_EQUAL(b.max_receive(), max_receive - 20);
}

TORRENT_TEST(receive_buffer_watermark)
{
	receive_buffer b;
	b.reset(0x4000);
	b.reserve(33500000);
	b.received(33500000);
	b.normalize();

	TEST_EQUAL(b.watermark(), 33500000);
}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

TORRENT_TEST(recv_buffer_mutable_buffers)
{
	receive_buffer b;
	b.reserve(1100);
	b.cut(0, 100); // packet size = 100
	b.received(1100);
	int packet_transferred = b.advance_pos(1100);
	// this is just the first packet
	TEST_EQUAL(packet_transferred, 100);
	// the next packet is 1000, and we're done with the first 100 bytes now
	b.cut(100, 1000); // packet size = 1000
	packet_transferred = b.advance_pos(999);
	TEST_EQUAL(packet_transferred, 999);
	span<char> vec = b.mutable_buffer(999);

	// previous packet
	//   |
	//   v   buffer
	// - - - -----------------------
	//       ^
	//       |
	// m_recv_start

	//       |----------------------| 1000 packet size
	//       |---------------------|  999 buffer

	TEST_EQUAL(vec.size(), 999);
}

#endif
