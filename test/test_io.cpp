/*

Copyright (c) 2018, Arvid Norberg
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
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/io.hpp"
#include "libtorrent/span.hpp"

using namespace lt::detail;
using namespace lt::aux;
using lt::span;

TORRENT_TEST(write_uint8)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	char* ptr = buf.data();
	write_uint8(0x10, ptr);
	TEST_CHECK(ptr == buf.data() + 1);
	TEST_CHECK(buf[0] == 0x10);
	TEST_CHECK(buf[1] == 0x55);
}

TORRENT_TEST(write_uint16)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	char* ptr = buf.data();
	write_uint16(0x2010, ptr);
	TEST_CHECK(ptr == buf.data() + 2);
	TEST_CHECK(buf[0] == 0x20);
	TEST_CHECK(buf[1] == 0x10);
	TEST_CHECK(buf[2] == 0x55);
}

TORRENT_TEST(write_uint32)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	char* ptr = buf.data();
	write_uint32(0x40302010, ptr);
	TEST_CHECK(ptr == buf.data() + 4);
	TEST_CHECK(buf[0] == 0x40);
	TEST_CHECK(buf[1] == 0x30);
	TEST_CHECK(buf[2] == 0x20);
	TEST_CHECK(buf[3] == 0x10);
	TEST_CHECK(buf[4] == 0x55);
}

TORRENT_TEST(write_int32)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	char* ptr = buf.data();
	write_int32(0x40302010, ptr);
	TEST_CHECK(ptr == buf.data() + 4);
	TEST_CHECK(buf[0] == 0x40);
	TEST_CHECK(buf[1] == 0x30);
	TEST_CHECK(buf[2] == 0x20);
	TEST_CHECK(buf[3] == 0x10);
	TEST_CHECK(buf[4] == 0x55);
}

TORRENT_TEST(write_uint64)
{
	std::array<std::uint8_t, 10> buf;
	buf.fill(0x55);
	std::uint8_t* ptr = buf.data();
	write_uint64(0x8070605040302010ull, ptr);
	TEST_CHECK(ptr == buf.data() + 8);
	TEST_CHECK(buf[0] == 0x80);
	TEST_CHECK(buf[1] == 0x70);
	TEST_CHECK(buf[2] == 0x60);
	TEST_CHECK(buf[3] == 0x50);
	TEST_CHECK(buf[4] == 0x40);
	TEST_CHECK(buf[5] == 0x30);
	TEST_CHECK(buf[6] == 0x20);
	TEST_CHECK(buf[7] == 0x10);
	TEST_CHECK(buf[8] == 0x55);
}

TORRENT_TEST(read_uint8)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x10;
	char const* ptr = buf.data();
	TEST_CHECK(read_uint8(ptr) == 0x10);
	TEST_CHECK(ptr == buf.data() + 1);
}

TORRENT_TEST(read_uint16)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x20;
	buf[1] = 0x10;
	char const* ptr = buf.data();
	TEST_CHECK(read_uint16(ptr) == 0x2010);
	TEST_CHECK(ptr == buf.data() + 2);
}

TORRENT_TEST(read_uint32)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x40;
	buf[1] = 0x30;
	buf[2] = 0x20;
	buf[3] = 0x10;
	char const* ptr = buf.data();
	TEST_CHECK(read_uint32(ptr) == 0x40302010);
	TEST_CHECK(ptr == buf.data() + 4);
}

TORRENT_TEST(read_uint64)
{
	std::array<std::uint8_t, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x80;
	buf[1] = 0x70;
	buf[2] = 0x60;
	buf[3] = 0x50;
	buf[4] = 0x40;
	buf[5] = 0x30;
	buf[6] = 0x20;
	buf[7] = 0x10;
	std::uint8_t const* ptr = buf.data();
	TEST_CHECK(read_uint64(ptr) == 0x8070605040302010ull);
	TEST_CHECK(ptr == buf.data() + 8);
}

TORRENT_TEST(read_int32)
{
	std::array<std::uint8_t, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x40;
	buf[1] = 0x30;
	buf[2] = 0x20;
	buf[3] = 0x10;
	std::uint8_t const* ptr = buf.data();
	TEST_CHECK(read_int32(ptr) == 0x40302010);
	TEST_CHECK(ptr == buf.data() + 4);
}

TORRENT_TEST(write_uint8_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	span<char> view(buf);
	write_uint8(0x10, view);
	TEST_CHECK(view == span<char>(buf).subspan(1));
	TEST_CHECK(buf[0] == 0x10);
	TEST_CHECK(buf[1] == 0x55);
}

TORRENT_TEST(write_uint16_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	span<char> view(buf);
	write_uint16(0x2010, view);
	TEST_CHECK(view == span<char>(buf).subspan(2));
	TEST_CHECK(buf[0] == 0x20);
	TEST_CHECK(buf[1] == 0x10);
	TEST_CHECK(buf[2] == 0x55);
}

TORRENT_TEST(write_uint32_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	span<char> view(buf);
	write_uint32(0x40302010, view);
	TEST_CHECK(view == span<char>(buf).subspan(4));
	TEST_CHECK(buf[0] == 0x40);
	TEST_CHECK(buf[1] == 0x30);
	TEST_CHECK(buf[2] == 0x20);
	TEST_CHECK(buf[3] == 0x10);
	TEST_CHECK(buf[4] == 0x55);
}

TORRENT_TEST(write_uint64_span)
{
	std::array<std::uint8_t, 10> buf;
	buf.fill(0x55);
	span<std::uint8_t> view(buf);
	write_uint64(0x8070605040302010ull, view);
	TEST_CHECK(view == span<std::uint8_t>(buf).subspan(8));
	TEST_CHECK(buf[0] == 0x80);
	TEST_CHECK(buf[1] == 0x70);
	TEST_CHECK(buf[2] == 0x60);
	TEST_CHECK(buf[3] == 0x50);
	TEST_CHECK(buf[4] == 0x40);
	TEST_CHECK(buf[5] == 0x30);
	TEST_CHECK(buf[6] == 0x20);
	TEST_CHECK(buf[7] == 0x10);
	TEST_CHECK(buf[8] == 0x55);
}

TORRENT_TEST(read_uint8_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x10;
	span<char const> view(buf);
	TEST_CHECK(read_uint8(view) == 0x10);
	TEST_CHECK(view == span<char>(buf).subspan(1));
}

TORRENT_TEST(read_uint16_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x20;
	buf[1] = 0x10;
	span<char const> view(buf);
	TEST_CHECK(read_uint16(view) == 0x2010);
	TEST_CHECK(view == span<char>(buf).subspan(2));
}

TORRENT_TEST(read_uint32_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x40;
	buf[1] = 0x30;
	buf[2] = 0x20;
	buf[3] = 0x10;
	span<char const> view(buf);
	TEST_CHECK(read_uint32(view) == 0x40302010);
	TEST_CHECK(view == span<char>(buf).subspan(4));
}

TORRENT_TEST(read_int32_span)
{
	std::array<char, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x40;
	buf[1] = 0x30;
	buf[2] = 0x20;
	buf[3] = 0x10;
	span<char const> view(buf);
	TEST_CHECK(read_int32(view) == 0x40302010);
	TEST_CHECK(view == span<char>(buf).subspan(4));
}

TORRENT_TEST(read_uint64_span)
{
	std::array<std::uint8_t, 10> buf;
	buf.fill(0x55);
	buf[0] = 0x80;
	buf[1] = 0x70;
	buf[2] = 0x60;
	buf[3] = 0x50;
	buf[4] = 0x40;
	buf[5] = 0x30;
	buf[6] = 0x20;
	buf[7] = 0x10;
	span<std::uint8_t const> view(buf);
	TEST_CHECK(read_uint64(view) == 0x8070605040302010ull);
	TEST_CHECK(view == span<std::uint8_t>(buf).subspan(8));
}
