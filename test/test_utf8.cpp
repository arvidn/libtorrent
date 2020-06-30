/*

Copyright (c) 2014-2017, 2019, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2018, Alden Torres
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
#include "libtorrent/ConvertUTF.h"
#include "setup_transfer.hpp" // for load_file
#include "libtorrent/aux_/path.hpp" // for combine_path

#include <vector>

using namespace lt;

namespace {

void verify_transforms(char const* utf8_source, int utf8_source_len = -1)
{
	if (utf8_source_len == -1)
		utf8_source_len = int(strlen(utf8_source));

	// utf8 -> utf16 -> utf32 -> utf8
	{
		std::vector<UTF16> utf16((std::size_t(utf8_source_len)));
		UTF8 const* in8 = reinterpret_cast<UTF8 const*>(utf8_source);
		UTF16* out16 = &utf16[0];
		ConversionResult ret = ConvertUTF8toUTF16(&in8, in8 + utf8_source_len
			, &out16, out16 + utf16.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		if (ret != conversionOK && utf8_source_len < 10)
		{
			for (char const* i = utf8_source; *i != 0; ++i)
				std::printf("%x ", UTF8(*i));
		}

		std::vector<UTF32> utf32((std::size_t(utf8_source_len)));
		UTF16 const* in16 = &utf16[0];
		UTF32* out32 = &utf32[0];
		ret = ConvertUTF16toUTF32(&in16, out16
			, &out32, out32 + utf32.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		if (ret != conversionOK && utf8_source_len < 10)
		{
			for (char const* i = utf8_source; *i != 0; ++i)
				std::printf("%x ", UTF8(*i));
		}

		std::vector<UTF8> utf8((std::size_t(utf8_source_len)));
		UTF32 const* in32 = &utf32[0];
		UTF8* out8 = &utf8[0];
		ret = ConvertUTF32toUTF8(&in32, out32
			, &out8, out8 + utf8.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		if (ret != conversionOK && utf8_source_len < 10)
		{
			for (char const* i = utf8_source; *i != 0; ++i)
				std::printf("%x ", UTF8(*i));
		}

		TEST_EQUAL(out8 - &utf8[0], utf8_source_len);
		TEST_CHECK(std::equal(&utf8[0], out8, reinterpret_cast<UTF8 const*>(utf8_source)));
	}

	// utf8 -> utf32 -> utf16 -> utf8
	{
		std::vector<UTF32> utf32((std::size_t(utf8_source_len)));
		UTF8 const* in8 = reinterpret_cast<UTF8 const*>(utf8_source);
		UTF32* out32 = &utf32[0];
		ConversionResult ret = ConvertUTF8toUTF32(&in8, in8 + utf8_source_len
			, &out32, out32 + utf32.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		if (ret != conversionOK && utf8_source_len < 10)
		{
			for (char const* i = utf8_source; *i != 0; ++i)
				std::printf("%x ", UTF8(*i));
		}

		std::vector<UTF16> utf16((std::size_t(utf8_source_len)));
		UTF32 const* in32 = &utf32[0];
		UTF16* out16 = &utf16[0];
		ret = ConvertUTF32toUTF16(&in32, out32
			, &out16, out16 + utf16.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		if (ret != conversionOK && utf8_source_len < 10)
		{
			for (char const* i = utf8_source; *i != 0; ++i)
				std::printf("%x ", UTF8(*i));
		}

		std::vector<UTF8> utf8((std::size_t(utf8_source_len)));
		UTF16 const* in16 = &utf16[0];
		UTF8* out8 = &utf8[0];
		ret = ConvertUTF16toUTF8(&in16, out16
			, &out8, out8 + utf8.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		if (ret != conversionOK && utf8_source_len < 10)
		{
			for (char const* i = utf8_source; *i != 0; ++i)
				std::printf("%x ", UTF8(*i));
		}

		TEST_EQUAL(out8 - &utf8[0], utf8_source_len);
		TEST_CHECK(std::equal(&utf8[0], out8, reinterpret_cast<UTF8 const*>(utf8_source)));
	}
}

void expect_error(char const* utf8, ConversionResult expect)
{
	UTF8 const* in8 = reinterpret_cast<UTF8 const*>(utf8);
	std::vector<UTF32> utf32(strlen(utf8));
	UTF32* out32 = &utf32[0];
	ConversionResult ret = ConvertUTF8toUTF32(&in8, in8 + std::strlen(utf8)
		, &out32, out32 + utf32.size(), strictConversion);

	TEST_EQUAL(ret, expect);
	if (ret != expect)
	{
		std::printf("%d expected %d\n", ret, expect);
		for (char const* i = utf8; *i != 0; ++i)
			std::printf("%x ", UTF8(*i));
	}

	in8 = reinterpret_cast<UTF8 const*>(utf8);
	std::vector<UTF16> utf16(std::strlen(utf8));
	UTF16* out16 = &utf16[0];
	ret = ConvertUTF8toUTF16(&in8, in8 + std::strlen(utf8)
		, &out16, out16 + utf16.size(), strictConversion);

	TEST_EQUAL(ret, expect);
	if (ret != expect)
	{
		std::printf("%d expected %d\n", ret, expect);
		for (char const* i = utf8; *i != 0; ++i)
			std::printf("%x ", UTF8(*i));
	}
}

} // anonymous namespace

TORRENT_TEST(utf8)
{
	std::vector<char> utf8_source;
	error_code ec;
	load_file(combine_path("..", "utf8_test.txt"), utf8_source, ec, 1000000);
	if (ec) std::printf("failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);

	std::vector<char> utf8_latin1_source;
	load_file(combine_path("..", "utf8_latin1_test.txt"), utf8_latin1_source, ec, 1000000);
	if (ec) std::printf("failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);


	// test lower level conversions

	verify_transforms(&utf8_source[0], int(utf8_source.size()));

	verify_transforms("\xc3\xb0");
	verify_transforms("\xed\x9f\xbf");
	verify_transforms("\xee\x80\x80");
	verify_transforms("\xef\xbf\xbd");
	verify_transforms("\xf4\x8f\xbf\xbf");
	verify_transforms("\xf0\x91\x80\x80\x30");

	// Unexpected continuation bytes
	expect_error("\x80", sourceIllegal);
	expect_error("\xbf", sourceIllegal);

	// Impossible bytes
	// The following two bytes cannot appear in a correct UTF-8 string
	expect_error("\xff", sourceExhausted);
	expect_error("\xfe", sourceExhausted);
	expect_error("\xff\xff\xfe\xfe", sourceExhausted);

	// Examples of an overlong ASCII character
	expect_error("\xc0\xaf", sourceIllegal);
	expect_error("\xe0\x80\xaf", sourceIllegal);
	expect_error("\xf0\x80\x80\xaf", sourceIllegal);
	expect_error("\xf8\x80\x80\x80\xaf ", sourceIllegal);
	expect_error("\xfc\x80\x80\x80\x80\xaf", sourceIllegal);

	// Maximum overlong sequences
	expect_error("\xc1\xbf", sourceIllegal);
	expect_error("\xe0\x9f\xbf", sourceIllegal);
	expect_error("\xf0\x8f\xbf\xbf", sourceIllegal);
	expect_error("\xf8\x87\xbf\xbf\xbf", sourceIllegal);
	expect_error("\xfc\x83\xbf\xbf\xbf\xbf", sourceIllegal);

	// Overlong representation of the NUL character
	expect_error("\xc0\x80", sourceIllegal);
	expect_error("\xe0\x80\x80", sourceIllegal);
	expect_error("\xf0\x80\x80\x80", sourceIllegal);
	expect_error("\xf8\x80\x80\x80\x80", sourceIllegal);
	expect_error("\xfc\x80\x80\x80\x80\x80", sourceIllegal);

	// Single UTF-16 surrogates
	expect_error("\xed\xa0\x80", sourceIllegal);
	expect_error("\xed\xad\xbf", sourceIllegal);
	expect_error("\xed\xae\x80", sourceIllegal);
	expect_error("\xed\xaf\xbf", sourceIllegal);
	expect_error("\xed\xb0\x80", sourceIllegal);
	expect_error("\xed\xbe\x80", sourceIllegal);
	expect_error("\xed\xbf\xbf", sourceIllegal);

	// Paired UTF-16 surrogates
	expect_error("\xed\xa0\x80\xed\xb0\x80", sourceIllegal);
	expect_error("\xed\xa0\x80\xed\xbf\xbf", sourceIllegal);
	expect_error("\xed\xad\xbf\xed\xb0\x80", sourceIllegal);
	expect_error("\xed\xad\xbf\xed\xbf\xbf", sourceIllegal);
	expect_error("\xed\xae\x80\xed\xb0\x80", sourceIllegal);
	expect_error("\xed\xae\x80\xed\xbf\xbf", sourceIllegal);
	expect_error("\xed\xaf\xbf\xed\xb0\x80", sourceIllegal);
	expect_error("\xed\xaf\xbf\xed\xbf\xbf", sourceIllegal);

	// test higher level conversions

	std::string utf8;
	std::copy(utf8_source.begin(), utf8_source.end(), std::back_inserter(utf8));

	std::wstring const wide = utf8_wchar(utf8);
	std::string const identity1 = wchar_utf8(wide);

	TEST_EQUAL(utf8, identity1);

	std::u32string const utf32 = utf8_utf32(utf8);
	std::string const identity2 = utf32_utf8(utf32);

	TEST_EQUAL(utf8, identity2);

	utf8.clear();
	std::copy(utf8_latin1_source.begin(), utf8_latin1_source.end(), std::back_inserter(utf8));

	std::string const latin1 = utf8_latin1(utf8);
	std::string const identity3 = latin1_utf8(latin1);

	TEST_EQUAL(utf8, identity3);

}

TORRENT_TEST(invalid_encoding)
{
	// thest invalid utf8 encodings. just treat it as "Latin-1"
	std::uint8_t const test_string[] = {
		0xd2, 0xe5, 0xf0, 0xea, 0xf1, 0x20, 0xe8, 0x20, 0xca, 0xe0, 0xe9, 0xea,
		0xee, 0xf1, 0x2e, 0x32, 0x30, 0x31, 0x34, 0x2e, 0x42, 0x44, 0x52, 0x69,
		0x70, 0x2e, 0x31, 0x30, 0x38, 0x30, 0x70, 0x2e, 0x6d, 0x6b, 0x76, 0x00
	};
	error_code ec;
	std::wstring wide = utf8_wchar(reinterpret_cast<char const*>(test_string), ec);
	TEST_CHECK(ec);

	std::wstring cmp_wide;
	std::copy(test_string, test_string + sizeof(test_string) - 1,
		std::back_inserter(cmp_wide));
	TEST_CHECK(wide == cmp_wide);
}
