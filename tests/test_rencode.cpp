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

#include "rencode.hpp"

#include "test.hpp"

enum renc_typecode
{
	CHR_LIST    = 59,
	CHR_DICT    = 60,
	CHR_INT     = 61,
	CHR_INT1    = 62,
	CHR_INT2    = 63,
	CHR_INT4    = 64,
	CHR_INT8    = 65,
	CHR_FLOAT32 = 66,
	CHR_FLOAT64 = 44,
	CHR_TRUE    = 67,
	CHR_FALSE   = 68,
	CHR_NONE    = 69,
	CHR_TERM    = 127,	
	// Positive integers with value embedded in typecode.
	INT_POS_FIXED_START = 0,
	INT_POS_FIXED_COUNT = 44,
	// Dictionaries with length embedded in typecode.
	DICT_FIXED_START = 102,
	DICT_FIXED_COUNT = 25,
	// Negative integers with value embedded in typecode.
	INT_NEG_FIXED_START = 70,
	INT_NEG_FIXED_COUNT = 32,
	// Strings with length embedded in typecode.
	STR_FIXED_START = 128,
	STR_FIXED_COUNT = 64,
	// Lists with length embedded in typecode.
	LIST_FIXED_START = STR_FIXED_START+STR_FIXED_COUNT,
	LIST_FIXED_COUNT = 64,
};

using namespace libtorrent;

int main_ret = 0;

int main(int argc, char* argv[])
{
	rtok_t tokens[100];
	int ret;

	char input1[] = { CHR_INT1, 0x40 };
	ret = rdecode(tokens, 100, input1, 2);
	print_rtok(tokens, input1);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_integer);
	TEST_CHECK(tokens[0].integer(input1) == 0x40);

	char input2[] = { CHR_INT2, 0x40, 0x80 };
	ret = rdecode(tokens, 100, input2, 3);
	print_rtok(tokens, input2);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_integer);
	TEST_CHECK(tokens[0].integer(input2) == 0x4080);

	char input3[] = { CHR_TRUE };
	ret = rdecode(tokens, 100, input3, 1);
	print_rtok(tokens, input3);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_bool);
	TEST_CHECK(tokens[0].boolean(input3) == true);

	char input4[] = { CHR_FALSE };
	ret = rdecode(tokens, 100, input4, 1);
	print_rtok(tokens, input4);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_bool);
	TEST_CHECK(tokens[0].boolean(input4) == false);

	char input5[] = { CHR_DICT, '3', ':', 'f', 'o', 'o', CHR_LIST, CHR_TRUE, CHR_FALSE, CHR_TERM, CHR_TERM };
	ret = rdecode(tokens, 100, input5, sizeof(input5));
	print_rtok(tokens, input5);
	printf("\n");
	TEST_CHECK(ret == 5);
	TEST_CHECK(tokens[0].type() == type_dict);
	TEST_CHECK(tokens[0].num_items() == 1);
	TEST_CHECK(tokens[1].type() == type_string);
	TEST_CHECK(tokens[1].string(input5) == "foo");
	TEST_CHECK(tokens[2].type() == type_list);
	TEST_CHECK(tokens[2].num_items() == 2);
	TEST_CHECK(tokens[3].type() == type_bool);
	TEST_CHECK(tokens[3].boolean(input5) == true);
	TEST_CHECK(tokens[4].type() == type_bool);
	TEST_CHECK(tokens[4].boolean(input5) == false);

	char input6[] = "6:foobar";
	ret = rdecode(tokens, 100, input6, sizeof(input6));
	print_rtok(tokens, input6);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_string);
	TEST_CHECK(tokens[0].string(input6) == "foobar");

	char input7[] = { CHR_INT, '2', '1', CHR_TERM};
	ret = rdecode(tokens, 100, input7, sizeof(input7));
	print_rtok(tokens, input7);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_integer);
	TEST_CHECK(tokens[0].integer(input7) == 21);

	char input8[] = { DICT_FIXED_START+1, '3', ':', 'f', 'o', 'o', LIST_FIXED_START+2, CHR_TRUE, CHR_FALSE };
	ret = rdecode(tokens, 100, input8, sizeof(input8));
	print_rtok(tokens, input8);
	printf("\n");
	TEST_CHECK(ret == 5);
	TEST_CHECK(tokens[0].type() == type_dict);
	TEST_CHECK(tokens[0].num_items() == 1);
	TEST_CHECK(tokens[1].type() == type_string);
	TEST_CHECK(tokens[1].string(input8) == "foo");
	TEST_CHECK(tokens[2].type() == type_list);
	TEST_CHECK(tokens[2].num_items() == 2);
	TEST_CHECK(tokens[3].type() == type_bool);
	TEST_CHECK(tokens[3].boolean(input8) == true);
	TEST_CHECK(tokens[4].type() == type_bool);
	TEST_CHECK(tokens[4].boolean(input8) == false);

	char input9[] = { CHR_NONE };
	ret = rdecode(tokens, 100, input9, 1);
	print_rtok(tokens, input9);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_none);

	char input10[] = { DICT_FIXED_START };
	ret = rdecode(tokens, 100, input10, 1);
	print_rtok(tokens, input10);
	printf("\n");
	TEST_CHECK(ret == 1);
	TEST_CHECK(tokens[0].type() == type_dict);
	TEST_CHECK(tokens[0].num_items() == 0);

	return main_ret;
}

