/*

Copyright (c) 2016, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "test.hpp"
#include "libtorrent/receive_buffer.hpp"

using namespace libtorrent;

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
	boost::asio::mutable_buffer vec
		= b.reserve(1000);

	b.received(1000);
	b.advance_pos(999);

	TEST_EQUAL(b.pos_at_end(), false);
}

TORRENT_TEST(recv_buffer_pos_at_end_true)
{
	receive_buffer b;
	b.cut(0, 1000);
	b.reserve(1000);
	boost::asio::mutable_buffer vec = b.reserve(1000);
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
	boost::asio::mutable_buffer vec = b.reserve(1000);
	b.received(1000);

	for (int i = 0; i < 10; ++i)
	{
		TEST_EQUAL(b.packet_finished(), false);
		b.advance_pos(1);
	}
	TEST_EQUAL(b.packet_finished(), true);
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
	boost::asio::mutable_buffer vec = b.mutable_buffers(999);

	// previous packet
	//   |
	//   v   buffer
	// - - - -----------------------
	//       ^
	//       |
	// m_recv_start

	//       |----------------------| 1000 packet size
	//       |---------------------|  999 buffer

	TEST_EQUAL(boost::asio::buffer_size(vec), 999);
}

#endif

