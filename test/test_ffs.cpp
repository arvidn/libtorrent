/*

Copyright (c) 2015, Arvid Norberg, Alden Torres
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
#include "libtorrent/span.hpp"
#include "libtorrent/hex.hpp" // from_hex
#include "libtorrent/aux_/ffs.hpp"
#include "libtorrent/aux_/byteswap.hpp"

using namespace lt;

static void to_binary(char const* s, std::uint32_t* buf)
{
	aux::from_hex({s, 40}, reinterpret_cast<char*>(&buf[0]));
}

TORRENT_TEST(count_leading_zeros)
{
	std::vector<std::pair<char const*, int>> const tests = {
		{ "ffffffffffffffffffffffffffffffffffffffff", 0 },
		{ "0000000000000000000000000000000000000000", 160 },
		{ "fff0000000000000000000000000000000000000", 0 },
		{ "7ff0000000000000000000000000000000000000", 1 },
		{ "3ff0000000000000000000000000000000000000", 2 },
		{ "1ff0000000000000000000000000000000000000", 3 },
		{ "0ff0000000000000000000000000000000000000", 4 },
		{ "07f0000000000000000000000000000000000000", 5 },
		{ "03f0000000000000000000000000000000000000", 6 },
		{ "01f0000000000000000000000000000000000000", 7 },
		{ "00f0000000000000000000000000000000000000", 8 },
		{ "0070000000000000000000000000000000000000", 9 },
		{ "0030000000000000000000000000000000000000", 10 },
		{ "0010000000000000000000000000000000000000", 11 },
		{ "0000000ffff00000000000000000000000000000", 28 },
		{ "00000007fff00000000000000000000000000000", 29 },
		{ "00000003fff00000000000000000000000000000", 30 },
		{ "00000001fff00000000000000000000000000000", 31 },
		{ "00000000fff00000000000000000000000000000", 32 },
		{ "000000007ff00000000000000000000000000000", 33 },
		{ "000000003ff00000000000000000000000000000", 34 },
		{ "000000001ff00000000000000000000000000000", 35 },
	};

	for (auto const& t : tests)
	{
		std::printf("%s\n", t.first);
		std::uint32_t buf[5];
		to_binary(t.first, buf);
		TEST_EQUAL(aux::count_leading_zeros_sw({buf, 5}), t.second);
		TEST_EQUAL(aux::count_leading_zeros_hw({buf, 5}), t.second);
		TEST_EQUAL(aux::count_leading_zeros({buf, 5}), t.second);
	}
}

TORRENT_TEST(count_trailing_ones_u32)
{
	std::uint32_t v = 0;
	TEST_EQUAL(aux::count_trailing_ones_sw(v), 0);
	TEST_EQUAL(aux::count_trailing_ones_hw(v), 0);
	TEST_EQUAL(aux::count_trailing_ones(v), 0);

	v = 0xffffffff;
	TEST_EQUAL(aux::count_trailing_ones_sw(v), 32);
	TEST_EQUAL(aux::count_trailing_ones_hw(v), 32);
	TEST_EQUAL(aux::count_trailing_ones(v), 32);

	v = aux::host_to_network(0xff00ff00);
	TEST_EQUAL(aux::count_trailing_ones_sw(v), 0);
	TEST_EQUAL(aux::count_trailing_ones_hw(v), 0);
	TEST_EQUAL(aux::count_trailing_ones(v), 0);

	v = aux::host_to_network(0xff0fff00);
	TEST_EQUAL(aux::count_trailing_ones_sw(v), 0);
	TEST_EQUAL(aux::count_trailing_ones_hw(v), 0);
	TEST_EQUAL(aux::count_trailing_ones(v), 0);

	v = aux::host_to_network(0xf0ff00ff);
	TEST_EQUAL(aux::count_trailing_ones_sw(v), 8);
	TEST_EQUAL(aux::count_trailing_ones_hw(v), 8);
	TEST_EQUAL(aux::count_trailing_ones(v), 8);

	v = aux::host_to_network(0xf0ff0fff);
	TEST_EQUAL(aux::count_trailing_ones_sw(v), 12);
	TEST_EQUAL(aux::count_trailing_ones_hw(v), 12);
	TEST_EQUAL(aux::count_trailing_ones(v), 12);

	std::uint32_t const arr[2] = {
		aux::host_to_network(0xf0ff0fff)
		, 0xffffffff};
	TEST_EQUAL(aux::count_trailing_ones_sw(arr), 44);
	TEST_EQUAL(aux::count_trailing_ones_hw(arr), 44);
	TEST_EQUAL(aux::count_trailing_ones(arr), 44);
}
