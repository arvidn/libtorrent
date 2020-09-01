/*

Copyright (c) 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
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

#include "libtorrent/crc32c.hpp"
#include "libtorrent/aux_/cpuid.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/byteswap.hpp"
#include "test.hpp"

TORRENT_TEST(crc32)
{
	using namespace lt;

	std::uint32_t out;

	std::uint32_t in1 = aux::host_to_network(0xeffea55a);
	out = crc32c_32(in1);
	TEST_EQUAL(out, 0x5ee3b9d5);

	std::uint64_t buf[4];
	memcpy(buf, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32);

	// https://tools.ietf.org/html/rfc3720#appendix-B.4
	out = crc32c(buf, 4);
	TEST_EQUAL(out, 0x8a9136aaU);

	memcpy(buf, "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 32);
	out = crc32c(buf, 4);
	TEST_EQUAL(out, 0x62a8ab43U);

	memcpy(buf, "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
		"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f", 32);
	out = crc32c(buf, 4);
	TEST_EQUAL(out, 0x46dd794eU);

#if !TORRENT_HAS_ARM
	TORRENT_ASSERT(!aux::arm_crc32c_support);
#endif
}
