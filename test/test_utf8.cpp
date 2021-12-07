/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2014-2015, 2017, 2019-2021, Arvid Norberg
Copyright (c) 2018, 2021, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2021, AllSeeingEyeTolledEweSew
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/utf8.hpp"
#include "setup_transfer.hpp" // for load_file
#include "libtorrent/aux_/path.hpp" // for combine_path

#include <vector>

using namespace lt;

namespace {
void test_utf8_roundtrip(std::int32_t const codepoint)
{
	std::string utf8;
	aux::append_utf8_codepoint(utf8, codepoint);

	auto const [cp, len] = aux::parse_utf8_codepoint(utf8);

	TEST_EQUAL(len, int(utf8.size()));
	TEST_EQUAL(cp, codepoint);
}

void test_parse_utf8(lt::string_view utf8)
{
	auto const [cp, len] = aux::parse_utf8_codepoint(utf8);
	TEST_EQUAL(len, int(utf8.size()));

	std::string out;
	aux::append_utf8_codepoint(out, cp);
	TEST_EQUAL(out, utf8);
}

void parse_error(lt::string_view utf8)
{
	auto const [cp, len] = aux::parse_utf8_codepoint(utf8);
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

TORRENT_TEST(utf8_latin1)
{
	std::vector<char> utf8_latin1_source;
	error_code ec;
	load_file(combine_path("..", "utf8_latin1_test.txt"), utf8_latin1_source, ec, 1000000);
	if (ec) std::printf("failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);

	std::string utf8;
	std::copy(utf8_latin1_source.begin(), utf8_latin1_source.end(), std::back_inserter(utf8));
	std::string const latin1 = aux::utf8_latin1(utf8);
	std::string const identity = aux::latin1_utf8(latin1);

	TEST_EQUAL(utf8, identity);
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
