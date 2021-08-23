/*

Copyright (c) 2014, Arvid Norberg
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
#include "libtorrent/utf8.hpp"
#include "setup_transfer.hpp" // for load_file
#include "libtorrent/aux_/path.hpp" // for combine_path

#include <vector>

using namespace lt;

namespace {
void test_utf8_roundtrip(std::int32_t const codepoint)
{
	std::string utf8;
	append_utf8_codepoint(utf8, codepoint);

	int len;
	std::int32_t cp;
	std::tie(cp, len) = parse_utf8_codepoint(utf8);

	TEST_EQUAL(len, int(utf8.size()));
	TEST_EQUAL(cp, codepoint);
}

void test_parse_utf8(lt::string_view utf8)
{
	int len;
	std::int32_t cp;
	std::tie(cp, len) = parse_utf8_codepoint(utf8);
	TEST_EQUAL(len, int(utf8.size()));

	std::string out;
	append_utf8_codepoint(out, cp);
	TEST_EQUAL(out, utf8);
}

void parse_error(lt::string_view utf8)
{
	int len;
	std::int32_t cp;
	std::tie(cp, len) = parse_utf8_codepoint(utf8);
	TEST_EQUAL(cp, -1);
	TEST_CHECK(len >= 1);
	TEST_CHECK(len <= int(utf8.size()));
}
}

TORRENT_TEST(parse_utf8_roundtrip)
{
	for (std::int32_t cp = 0; cp < 0xd800; ++cp)
		test_utf8_roundtrip(cp);

	// skip surrogate codepoints, which are invalid. They won't round-trip

	for (std::int32_t cp = 0xe000; cp < 0xffff; ++cp)
		test_utf8_roundtrip(cp);
}

TORRENT_TEST(parse_utf8)
{
	test_parse_utf8("\x7f");
	test_parse_utf8("\xc3\xb0");
	test_parse_utf8("\xed\x9f\xbf");
	test_parse_utf8("\xee\x80\x80");
	test_parse_utf8("\xef\xbf\xbd");
	test_parse_utf8("\xf4\x8f\xbf\xbf");

	// largest possible codepoint
	test_parse_utf8("\xf4\x8f\xbf\xbf");
}

TORRENT_TEST(parse_utf8_fail)
{
	// Unexpected continuation bytes
	parse_error("\x80");
	parse_error("\xbf");

	// Impossible bytes
	// The following two bytes cannot appear in a correct UTF-8 string
	parse_error("\xff");
	parse_error("\xfe");
	parse_error("\xff\xff\xfe\xfe");

	// Examples of an overlong ASCII character
	parse_error("\xc0\xaf");
	parse_error("\xe0\x80\xaf");
	parse_error("\xf0\x80\x80\xaf");
	parse_error("\xf8\x80\x80\x80\xaf ");
	parse_error("\xfc\x80\x80\x80\x80\xaf");

	// Maximum overlong sequences
	parse_error("\xc1\xbf");
	parse_error("\xe0\x9f\xbf");
	parse_error("\xf0\x8f\xbf\xbf");
	parse_error("\xf8\x87\xbf\xbf\xbf");
	parse_error("\xfc\x83\xbf\xbf\xbf\xbf");

	// Overlong representation of the NUL character
	parse_error("\xc0\x80");
	parse_error("\xe0\x80\x80");
	parse_error("\xf0\x80\x80\x80");
	parse_error("\xf8\x80\x80\x80\x80");
	parse_error("\xfc\x80\x80\x80\x80\x80");

	// invalid continuation character
	parse_error("\xc0\x7f");

	// codepoint too high
	parse_error("\xf5\x8f\xbf\xbf");
	parse_error("\xf4\xbf\xbf\xbf");

	// surrogates not allowed
	parse_error("\xed\xb8\x88");
}
