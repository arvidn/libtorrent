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
#include "libtorrent/ConvertUTF.h"
#include "setup_transfer.hpp" // for load_file
#include "file.hpp" // for combine_path

#include <vector>

using namespace libtorrent;

int test_main()
{
	std::vector<char> utf8_source;
	error_code ec;
	load_file(combine_path("..", "utf8_test.txt"), utf8_source, ec, 1000000);
	if (ec) fprintf(stderr, "failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);

	// test lower level conversions

	// utf8 -> utf16 -> utf32 -> utf8
	{
		std::vector<UTF16> utf16(utf8_source.size());
		UTF8 const* in8 = (UTF8 const*)&utf8_source[0];
		UTF16* out16 = &utf16[0];
		ConversionResult ret = ConvertUTF8toUTF16(&in8, in8 + utf8_source.size()
			, &out16, out16 + utf16.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);

		std::vector<UTF32> utf32(utf8_source.size());
		UTF16 const* in16 = &utf16[0];
		UTF32* out32 = &utf32[0];
		ret = ConvertUTF16toUTF32(&in16, out16
			, &out32, out32 + utf32.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);

		std::vector<UTF8> utf8(utf8_source.size());
		UTF32 const* in32 = &utf32[0];
		UTF8* out8 = &utf8[0];
		ret = ConvertUTF32toUTF8(&in32, out32
			, &out8, out8 + utf8.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		TEST_EQUAL(out8 - &utf8[0], utf8_source.size());
		TEST_CHECK(std::equal(&utf8[0], out8, (UTF8 const*)&utf8_source[0]));
	}

	// utf8 -> utf32 -> utf16 -> utf8
	{
		std::vector<UTF32> utf32(utf8_source.size());
		UTF8 const* in8 = (UTF8 const*)&utf8_source[0];
		UTF32* out32 = &utf32[0];
		ConversionResult ret = ConvertUTF8toUTF32(&in8, in8 + utf8_source.size()
			, &out32, out32 + utf32.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);

		std::vector<UTF16> utf16(utf8_source.size());
		UTF32 const* in32 = &utf32[0];
		UTF16* out16 = &utf16[0];
		ret = ConvertUTF32toUTF16(&in32, out32
			, &out16, out16 + utf16.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);

		std::vector<UTF8> utf8(utf8_source.size());
		UTF16 const* in16 = &utf16[0];
		UTF8* out8 = &utf8[0];
		ret = ConvertUTF16toUTF8(&in16, out16
			, &out8, out8 + utf8.size(), strictConversion);

		TEST_EQUAL(ret, conversionOK);
		TEST_EQUAL(out8 - &utf8[0], utf8_source.size());
		TEST_CHECK(std::equal(&utf8[0], out8, (UTF8 const*)&utf8_source[0]));
	}

	// test higher level conversions

	std::string utf8;
	std::copy(utf8_source.begin(), utf8_source.end(), std::back_inserter(utf8));

	std::wstring wide;
	utf8_conv_result_t ret = utf8_wchar(utf8, wide);
	TEST_EQUAL(ret, conversion_ok);

	std::string identity;
	ret = wchar_utf8(wide, identity);
	TEST_EQUAL(ret, conversion_ok);

	TEST_EQUAL(utf8, identity);
	return 0;
}

