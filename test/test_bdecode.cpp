/*

Copyright (c) 2015-2019, 2022, Arvid Norberg
Copyright (c) 2017, Steven Siloti
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
#include "libtorrent/bdecode.hpp"
#include "libtorrent/entry.hpp"

using namespace lt;

// test integer
TORRENT_TEST(integer)
{
	char b[] = "i12453e";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(span<char>(b, sizeof(b) - 1) == e.data_section());
	TEST_EQUAL(e.type(), bdecode_node::int_t);
	TEST_EQUAL(e.int_value(), 12453);
}

TORRENT_TEST(construct_string)
{
	entry e(std::string("abc123"));
	TEST_EQUAL(e.string(), "abc123");
}

TORRENT_TEST(construct_string_literal)
{
	entry e("abc123");
	TEST_EQUAL(e.string(), "abc123");
}


TORRENT_TEST(construct_string_view)
{
	entry e(string_view("abc123"));
	TEST_EQUAL(e.string(), "abc123");
}

TORRENT_TEST(construct_integer)
{
	entry e(4);
	TEST_EQUAL(e.integer(), 4);
}

// test string
TORRENT_TEST(string)
{
	char b[] = "26:abcdefghijklmnopqrstuvwxyz";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(span<char>(b, sizeof(b) - 1) == e.data_section());
	TEST_EQUAL(e.type(), bdecode_node::string_t);
	TEST_EQUAL(e.string_value(), std::string("abcdefghijklmnopqrstuvwxyz"));
	TEST_EQUAL(e.string_length(), 26);
}

// test string-prefix
TORRENT_TEST(string_prefix1)
{
	// test edge-case of a string that's nearly too long
	std::string test;
	test.resize(1000000 + 8);
	memcpy(&test[0], "1000000:", 8);
	// test is a valid bencoded string, that's quite long
	error_code ec;
	bdecode_node e = bdecode(test, ec);
	TEST_CHECK(!ec);
	std::printf("%d bytes string\n", e.string_length());
	TEST_CHECK(span<char const>(test) == e.data_section());
	TEST_EQUAL(e.type(), bdecode_node::string_t);
	TEST_EQUAL(e.string_length(), 1000000);
	TEST_EQUAL(e.string_ptr(), test.c_str() + 8);
}

// test list
TORRENT_TEST(list)
{
	char b[] = "li12453e3:aaae";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(span<char>(b, sizeof(b) - 1) == e.data_section());
	TEST_EQUAL(e.type(), bdecode_node::list_t);
	TEST_EQUAL(e.list_size(), 2);
	TEST_EQUAL(e.list_at(0).type(), bdecode_node::int_t);
	TEST_EQUAL(e.list_at(1).type(), bdecode_node::string_t);
	TEST_EQUAL(e.list_at(0).int_value(), 12453);
	TEST_EQUAL(e.list_at(1).string_value(), std::string("aaa"));
	TEST_EQUAL(e.list_at(1).string_length(), 3);
	TEST_CHECK(span<char const>("3:aaa", 5) == e.list_at(1).data_section());
}

// test dict
TORRENT_TEST(dict)
{
	char b[] = "d1:ai12453e1:b3:aaa1:c3:bbb1:X10:0123456789e";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(span<char>(b, sizeof(b) - 1) == e.data_section());
	TEST_EQUAL(e.type(), bdecode_node::dict_t);
	TEST_EQUAL(e.dict_size(), 4);
	TEST_EQUAL(e.dict_find("a").type(), bdecode_node::int_t);
	TEST_EQUAL(e.dict_find("a").int_value(), 12453);
	TEST_EQUAL(e.dict_find("b").type(), bdecode_node::string_t);
	TEST_EQUAL(e.dict_find("b").string_value(), std::string("aaa"));
	TEST_EQUAL(e.dict_find("b").string_length(), 3);
	TEST_EQUAL(e.dict_find("c").type(), bdecode_node::string_t);
	TEST_EQUAL(e.dict_find("c").string_value(), std::string("bbb"));
	TEST_EQUAL(e.dict_find("c").string_length(), 3);
	TEST_EQUAL(e.dict_find_string_value("X"), "0123456789");
	char error_string[200];
	TEST_CHECK(e.has_soft_error(error_string));
	TEST_EQUAL(std::string(error_string), std::string("unsorted dictionary key"));
}

// test dictionary with a key without a value
TORRENT_TEST(dict_key_novalue)
{
	char b[] = "d1:ai1e1:be";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 10);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_value));
	std::printf("%s\n", print_entry(e).c_str());
}

// test dictionary with a key that's not a string
TORRENT_TEST(dict_nonstring_key)
{
	char b[] = "di5e1:ae";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 1);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// dictionary key with \0
TORRENT_TEST(dict_null_key)
{
	char b[] = "d3:a\0bi1ee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(e.dict_size() == 1);
	bdecode_node d = e.dict_find(std::string("a\0b", 3));
	TEST_EQUAL(d.type(), bdecode_node::int_t);
	TEST_EQUAL(d.int_value(), 1);
}

// soft error reported for dictionary with unordered keys
TORRENT_TEST(dict_unordered_keys)
{
	char error_string[200];
	{
		char b[] = "d2:abi1e2:aai2ee";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		TEST_CHECK(e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("unsorted dictionary key"));
	}
	{
		char b[] = "d2:bai1e2:aai2ee";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		TEST_CHECK(e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("unsorted dictionary key"));
	}
	{
		char b[] = "d2:aai1e1:ai2ee";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		TEST_CHECK(e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("unsorted dictionary key"));
	}
	{
		char b[] = "d1:ai1e2:aai2ee";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		TEST_CHECK(!e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("unsorted dictionary key"));
	}
	{
		char b[] = "d2:aai1e1:bi2ee";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		TEST_CHECK(!e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("unsorted dictionary key"));
	}
}

TORRENT_TEST(dict_duplicate_key)
{
	char b[] = "d2:aai1e2:aai2ee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	char error_string[200];
	TEST_CHECK(e.has_soft_error(error_string));
	TEST_EQUAL(std::string(error_string), std::string("duplicate dictionary key"));
}

// premature e
TORRENT_TEST(premature_e)
{
	char b[] = "e";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test strings with negative length-prefix
TORRENT_TEST(negative_length_prefix)
{
	char b[] = "-10:foobar";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 0);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_value));
	std::printf("%s\n", print_entry(e).c_str());
}

// test strings with overflow length-prefix
TORRENT_TEST(overflow_length_prefix)
{
	char b[] = "18446744073709551615:foobar";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 19);
	TEST_EQUAL(ec,  error_code(bdecode_errors::overflow));
	std::printf("%s\n", print_entry(e).c_str());
}

// test strings with almost overflow (more than 8 digits)
TORRENT_TEST(close_overflow_length_prefix)
{
	char b[] = "99999999:foobar";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 8);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test strings with overflow (more than 8 digits)
TORRENT_TEST(overflow_length_prefix2)
{
	char b[] = "199999999:foobar";
	error_code ec;
	int pos;
	// pretend that we have a large buffer like that
	bdecode_node e = bdecode({b, 999999999}, ec, &pos);
	TEST_EQUAL(pos, 0);
	TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded));
	std::printf("%s\n", print_entry(e).c_str());
}

TORRENT_TEST(leading_zero_length_prefix)
{
	{
		char b[] = "06:foobar";
		error_code ec;
		int pos;
		bdecode_node e = bdecode(b, ec, &pos);
		char error_string[200];
		TEST_CHECK(e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("leading zero in string length"));
		std::printf("%s\n", print_entry(e).c_str());
	}
	{
		char b[] = "0:";
		error_code ec;
		int pos;
		bdecode_node e = bdecode(b, ec, &pos);
		char error_string[200];
		TEST_CHECK(!e.has_soft_error(error_string));
		std::printf("%s\n", print_entry(e).c_str());
	}
}

// test integer without any digits
TORRENT_TEST(nodigit_int)
{
	char b[] = "ie";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 1);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// test integer with just a minus
TORRENT_TEST(minus_int)
{
	char b[] = "i-e";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 2);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// test integer with a minus inserted in it
TORRENT_TEST(interior_minus_int)
{
	char b[] = "i35412-5633e";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 6);
	TEST_EQUAL(ec,  error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// test integers that don't fit in 64 bits
TORRENT_TEST(int_overflow)
{
	char b[] = "i18446744073709551615e";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	std::printf("%s\n", print_entry(e).c_str());
	// the lazy aspect makes this overflow when asking for
	// the value. turning it to zero.
	TEST_EQUAL(e.int_value(), 0);
}

// test integers with more than 20 digits (overflow on parsing)
TORRENT_TEST(int_overflow2)
{
	char b[] = "i184467440737095516154e";
	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 22);
	TEST_EQUAL(ec,  error_code(bdecode_errors::overflow));
	std::printf("%s\n", print_entry(e).c_str());
}

// test truncated negative integer
TORRENT_TEST(int_truncated)
{
	char b[] = "i-";
	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, 2}, ec, &pos);
	TEST_EQUAL(pos, 2);
	TEST_EQUAL(ec,  error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

TORRENT_TEST(int_leading_zero)
{
	{
		char b[] = "i01e";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		char error_string[200];
		TEST_CHECK(e.has_soft_error(error_string));
		TEST_EQUAL(std::string(error_string), std::string("leading zero in integer"));
		std::printf("%s\n", print_entry(e).c_str());
	}
	{
		char b[] = "i0e";
		error_code ec;
		bdecode_node e = bdecode(b, ec);
		char error_string[200];
		TEST_CHECK(!e.has_soft_error(error_string));
		std::printf("%s\n", print_entry(e).c_str());
	}
}

// bdecode_error
TORRENT_TEST(bdecode_error)
{
	error_code ec(bdecode_errors::overflow);
	TEST_EQUAL(ec.message(), "integer overflow");
	TEST_EQUAL(ec.category().name(), std::string("bdecode"));
	ec.assign(5434, bdecode_category());
	TEST_EQUAL(ec.message(), "Unknown error");
}

// test integers that just exactly fit in 64 bits
TORRENT_TEST(64bit_int)
{
	char b[] = "i9223372036854775807e";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(e.int_value() == 9223372036854775807LL);
}

// test integers that just exactly fit in 64 bits
TORRENT_TEST(64bit_int_negative)
{
	char b[] = "i-9223372036854775807e";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(e.int_value() == -9223372036854775807LL);
}

// test integers that have invalid digits
TORRENT_TEST(int_invalid_digit)
{
	char b[] = "i92337203t854775807e";
	error_code ec;
	int pos = 0;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 9);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// test invalid encoding
TORRENT_TEST(invalid_encoding)
{
	unsigned char buf[] =
		{ 0x64, 0x31, 0x3a, 0x61, 0x64, 0x32, 0x3a, 0x69
		, 0x64, 0x32, 0x30, 0x3a, 0x2a, 0x21, 0x19, 0x89
		, 0x9f, 0xcd, 0x5f, 0xc9, 0xbc, 0x80, 0xc1, 0x76
		, 0xfe, 0xe0, 0xc6, 0x84, 0x2d, 0xf6, 0xfc, 0xb8
		, 0x39, 0x3a, 0x69, 0x6e, 0x66, 0x6f, 0x5f, 0x68
		, 0x61, 0xae, 0x68, 0x32, 0x30, 0x3a, 0x14, 0x78
		, 0xd5, 0xb0, 0xdc, 0xf6, 0x82, 0x42, 0x32, 0xa0
		, 0xd6, 0x88, 0xeb, 0x48, 0x57, 0x01, 0x89, 0x40
		, 0x4e, 0xbc, 0x65, 0x31, 0x3a, 0x71, 0x39, 0x3a
		, 0x67, 0x65, 0x74, 0x5f, 0x70, 0x65, 0x65, 0x72
		, 0x78, 0xff, 0x3a, 0x74, 0x38, 0x3a, 0xaa, 0xd4
		, 0xa1, 0x88, 0x7a, 0x8d, 0xc3, 0xd6, 0x31, 0x3a
		, 0x79, 0x31, 0xae, 0x71, 0x65, 0};

	std::printf("%s\n", buf);
	error_code ec;
	bdecode_node e = bdecode({reinterpret_cast<char const*>(buf), sizeof(buf)}, ec);
	TEST_CHECK(ec);
}

// test the depth limit
TORRENT_TEST(depth_limit)
{
	char b[2048];
	for (int i = 0; i < 1024; ++i)
		b[i]= 'l';

	for (int i = 1024; i < 2048; ++i)
		b[i]= 'e';

	// 1024 levels nested lists

	error_code ec;
	bdecode_node e = bdecode({b, sizeof(b)}, ec, nullptr, 100);
	TEST_EQUAL(ec, error_code(bdecode_errors::depth_exceeded));
}

// test the item limit
TORRENT_TEST(item_limit)
{
	char b[10240];
	b[0] = 'l';
	std::ptrdiff_t i = 1;
	for (i = 1; i < 10239; i += 2)
		memcpy(&b[i], "0:", 2);
	b[i] = 'e';

	error_code ec;
	bdecode_node e = bdecode({b, i + 1}, ec, nullptr, 1000, 1000);
	TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded));
}

// test unexpected EOF
TORRENT_TEST(unepected_eof)
{
	char b[] = "l2:.."; // expected terminating 'e'

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, 5}, ec, &pos);
	TEST_EQUAL(pos, 5);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test unexpected EOF in string length
TORRENT_TEST(unepected_eof2)
{
	char b[] = "l2:..0"; // expected ':' delimiter instead of EOF

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, 6}, ec, &pos);
	TEST_EQUAL(pos, 6);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test expected string
TORRENT_TEST(expected_string)
{
	char b[] = "di2ei0ee";
	// expected string (dict keys must be strings)

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 1);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// test unexpected EOF while parsing dict key
TORRENT_TEST(unexpected_eof_dict_key)
{
	char b[] = "d1000:..e";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 5);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test unexpected EOF while parsing dict key
TORRENT_TEST(unexpected_eof_dict_key2)
{
	char b[] = "d1000:";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 5);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test expected string while parsing dict key
TORRENT_TEST(expected_string_dict_key2)
{
	char b[] = "df00:";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 1);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	std::printf("%s\n", print_entry(e).c_str());
}

// test unexpected EOF while parsing int
TORRENT_TEST(unexpected_eof_int)
{
	char b[] = "i";

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, 1}, ec, &pos);
	TEST_EQUAL(pos, 1);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test unexpected EOF while parsing int
TORRENT_TEST(unexpected_eof_int2)
{
	char b[] = "i10";

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, 3}, ec, &pos);
	TEST_EQUAL(pos, 3);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}


// test expected colon
TORRENT_TEST(expected_colon_dict)
{
	char b[] = "d1000";

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, 5}, ec, &pos);
	TEST_EQUAL(pos, 5);
	TEST_EQUAL(ec, error_code(bdecode_errors::expected_colon));
	std::printf("%s\n", print_entry(e).c_str());
}

// test empty string
TORRENT_TEST(empty_string)
{
	error_code ec;
	bdecode_node e = bdecode({}, ec, nullptr);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

// test partial string
TORRENT_TEST(partial_string)
{
	char b[] = "100:..";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 3);
	TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	std::printf("%s\n", print_entry(e).c_str());
}

TORRENT_TEST(list_ints)
{
	std::string buf;
	buf += "l";
	for (int i = 0; i < 1000; ++i)
	{
		char tmp[20];
		std::snprintf(tmp, sizeof(tmp), "i%de", i);
		buf += tmp;
	}
	buf += "e";

	error_code ec;
	bdecode_node e = bdecode(buf, ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(e.type(), bdecode_node::list_t);
	TEST_EQUAL(e.list_size(), 1000);
	for (int i = 0; i < 1000; ++i)
	{
		TEST_EQUAL(e.list_int_value_at(i), i);
	}
}

TORRENT_TEST(dict_ints)
{
	std::string buf;
	buf += "d";
	for (int i = 0; i < 1000; ++i)
	{
		char tmp[30];
		std::snprintf(tmp, sizeof(tmp), "4:%04di%de", i, i);
		buf += tmp;
	}
	buf += "e";

	std::printf("%s\n", buf.c_str());
	error_code ec;
	bdecode_node e = bdecode(buf, ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);
	TEST_EQUAL(e.dict_size(), 1000);
	for (int i = 0; i < 1000; ++i)
	{
		char tmp[30];
		std::snprintf(tmp, sizeof(tmp), "%04d", i);
		TEST_EQUAL(e.dict_find_int_value(tmp), i);
	}
}

// test dict_at
TORRENT_TEST(dict_at)
{
	char b[] = "d3:fooi1e3:bari2ee";

	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);

	TEST_EQUAL(e.type(), bdecode_node::dict_t);
	TEST_EQUAL(e.dict_size(), 2);
	TEST_EQUAL(e.dict_at(0).first, "foo");
	TEST_EQUAL(e.dict_at(0).second.type(), bdecode_node::int_t);
	TEST_EQUAL(e.dict_at(0).second.int_value(), 1);
	TEST_EQUAL(e.dict_at(1).first, "bar");
	TEST_EQUAL(e.dict_at(1).second.type(), bdecode_node::int_t);
	TEST_EQUAL(e.dict_at(1).second.int_value(), 2);
}

// test string_ptr
TORRENT_TEST(string_ptr)
{
	char b[] = "l3:fooe";

	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);

	TEST_EQUAL(e.type(), bdecode_node::list_t);
	TEST_EQUAL(e.list_size(), 1);
	TEST_EQUAL(e.list_at(0).type(), bdecode_node::string_t);
	TEST_EQUAL(e.list_at(0).string_ptr(), b + 3);
	TEST_EQUAL(e.list_at(0).string_length(), 3);
}

// test exceeding buffer size limit
TORRENT_TEST(exceed_buf_limit)
{
	char b[] = "l3:fooe";

	error_code ec;
	bdecode_node e = bdecode({b, 0x3fffffff}, ec);
	TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded));
	std::printf("%s\n", print_entry(e).c_str());
}

// test parse_int
TORRENT_TEST(parse_int)
{
	char b[] = "1234567890e";
	std::int64_t val = 0;
	bdecode_errors::error_code_enum ec = bdecode_errors::no_error;
	char const* e = parse_int(b, b + sizeof(b)-1, 'e', val, ec);
	TEST_EQUAL(ec, bdecode_errors::no_error);
	TEST_EQUAL(val, 1234567890);
	TEST_EQUAL(e, b + sizeof(b) - 2);
}

// test invalid digit
TORRENT_TEST(invalid_digit)
{
	char b[] = "0o";
	std::int64_t val = 0;
	bdecode_errors::error_code_enum ec;
	char const* e = parse_int(b, b + sizeof(b)-1, 'e', val, ec);
	TEST_EQUAL(ec, bdecode_errors::expected_digit);
	TEST_EQUAL(e, b + 1);
}

// test parse_int overflow
TORRENT_TEST(parse_int_overflow)
{
	char b[] = "9223372036854775808:";
	std::int64_t val = 0;
	bdecode_errors::error_code_enum ec;
	char const* e = parse_int(b, b + sizeof(b)-1, ':', val, ec);
	TEST_EQUAL(ec, bdecode_errors::overflow);
	TEST_EQUAL(e, b + 18);
}

TORRENT_TEST(parse_length_overflow)
{
	string_view const b[] = {
		"d1:a1919191010:11111"_sv,
		"d2143289344:a4:aaaae"_sv,
		"d214328934114:a4:aaaae"_sv,
		"d9205357638345293824:a4:aaaae"_sv,
		"d1:a9205357638345293824:11111"_sv
	};

	for (auto const& buf : b)
	{
		error_code ec;
		bdecode_node e = bdecode(buf, ec);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}
}


TORRENT_TEST(expected_colon_string)
{
	char b[] = "928";
	std::int64_t val = 0;
	bdecode_errors::error_code_enum ec = bdecode_errors::no_error;
	char const* e = parse_int(b, b + sizeof(b)-1, ':', val, ec);
	TEST_EQUAL(ec, bdecode_errors::no_error);
	TEST_EQUAL(e, b + 3);
}

// test dict_find_* functions
TORRENT_TEST(dict_find_funs)
{
	// a: int
	// b: string
	// c: list
	// d: dict
	char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	// dict_find_int*

	TEST_EQUAL(e.dict_find_int_value("a"), 1);
	TEST_EQUAL(e.dict_find_int("a").type(), bdecode_node::int_t);
	TEST_EQUAL(e.dict_find_int_value("b", -10), -10);
	TEST_EQUAL(e.dict_find_int_value("x", -10), -10);
	TEST_EQUAL(e.dict_find_int("b").type(), bdecode_node::none_t);
	TEST_EQUAL(e.dict_find_int("x").type(), bdecode_node::none_t);

	// dict_find_string*

	TEST_EQUAL(e.dict_find_string_value("b"), "foo");
	TEST_EQUAL(e.dict_find_string("b").type(), bdecode_node::string_t);
	TEST_EQUAL(e.dict_find_string_value("c", "blah"), "blah");
	TEST_EQUAL(e.dict_find_string_value("x", "blah"), "blah");
	TEST_EQUAL(e.dict_find_string("c").type(), bdecode_node::none_t);
	TEST_EQUAL(e.dict_find_string("x").type(), bdecode_node::none_t);

	// dict_find_list

	TEST_CHECK(e.dict_find_list("c"));
	TEST_EQUAL(e.dict_find_list("c").list_size(), 2);
	TEST_EQUAL(e.dict_find_list("c").list_int_value_at(0), 1);
	TEST_EQUAL(e.dict_find_list("c").list_int_value_at(1), 2);
	TEST_CHECK(!e.dict_find_list("d"));

	// dict_find_dict

	TEST_CHECK(e.dict_find_dict("d"));
	TEST_EQUAL(e.dict_find_dict("d").dict_find_int_value("x"), 1);
	TEST_EQUAL(e.dict_find_dict("d").dict_find_int_value("y", -10), -10);
	TEST_CHECK(!e.dict_find_dict("c"));

	// variants taking std::string
	TEST_EQUAL(e.dict_find_dict(std::string("d")).dict_find_int_value("x"), 1);
	TEST_CHECK(!e.dict_find_dict(std::string("c")));
	TEST_CHECK(!e.dict_find_dict(std::string("x")));

	TEST_EQUAL(e.dict_size(), 4);
	TEST_EQUAL(e.dict_size(), 4);

	// dict_at

	TEST_EQUAL(e.dict_at(0).first, "a");
	TEST_EQUAL(e.dict_at(0).second.int_value(), 1);
	TEST_EQUAL(e.dict_at(1).first, "b");
	TEST_EQUAL(e.dict_at(1).second.string_value(), "foo");
	TEST_EQUAL(e.dict_at(2).first, "c");
	TEST_EQUAL(e.dict_at(2).second.type(), bdecode_node::list_t);
	TEST_EQUAL(e.dict_at(3).first, "d");
	TEST_EQUAL(e.dict_at(3).second.type(), bdecode_node::dict_t);
}

// test list_*_at functions
TORRENT_TEST(list_at_funs)
{
	// int
	// string
	// list
	// dict
	char b[] = "li1e3:fooli1ei2eed1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.type(), bdecode_node::list_t);

	TEST_EQUAL(e.list_int_value_at(0), 1);
	// make sure default values work
	TEST_EQUAL(e.list_int_value_at(1, -10), -10);

	TEST_EQUAL(e.list_string_value_at(1), "foo");
	// make sure default values work
	TEST_EQUAL(e.list_string_value_at(2, "blah"), "blah");

	TEST_EQUAL(e.list_at(2).type(), bdecode_node::list_t);
	TEST_EQUAL(e.list_at(2).list_size(), 2);
	TEST_EQUAL(e.list_at(2).list_int_value_at(0), 1);
	TEST_EQUAL(e.list_at(2).list_int_value_at(1), 2);

	TEST_EQUAL(e.list_at(3).type(), bdecode_node::dict_t);
	TEST_EQUAL(e.list_at(3).dict_size(), 1);
	TEST_EQUAL(e.list_at(3).dict_find_int_value("x"), 1);
	TEST_EQUAL(e.list_at(3).dict_find_int_value("y", -10), -10);

	TEST_EQUAL(e.list_size(), 4);
	TEST_EQUAL(e.list_size(), 4);
}

// test list_at in reverse order
TORRENT_TEST(list_at_reverse)
{
	// int
	// string
	// list
	// dict
	char b[] = "li1e3:fooli1ei2eed1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.type(), bdecode_node::list_t);

	TEST_EQUAL(e.list_at(3).type(), bdecode_node::dict_t);
	TEST_EQUAL(e.list_at(2).type(), bdecode_node::list_t);
	TEST_EQUAL(e.list_string_value_at(1), "foo");
	TEST_EQUAL(e.list_int_value_at(0), 1);

	TEST_EQUAL(e.list_size(), 4);
	TEST_EQUAL(e.list_size(), 4);
}


// test dict_find_* functions
TORRENT_TEST(dict_find_funs2)
{
	// a: int
	// b: string
	// c: list
	// d: dict
	char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	// try finding the last item in a dict (to skip all the other ones)
	TEST_EQUAL(e.dict_find("d").type(), bdecode_node::dict_t);
	TEST_EQUAL(e.dict_find(std::string("d")).type(), bdecode_node::dict_t);
}

// print_entry

TORRENT_TEST(print_entry)
{
	char b[] = "li1e3:fooli1ei2eed1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(print_entry(e), "[ 1, 'foo', [ 1, 2 ], { 'x': 1 } ]");
}

TORRENT_TEST(print_entry2)
{
	char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ], 'd': { 'x': 1 } }");
}

// test swap()
TORRENT_TEST(swap)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	char b2[] = "i1e";

	error_code ec;

	bdecode_node e1 = bdecode({b1, sizeof(b1)-1}, ec);
	TEST_CHECK(!ec);
	bdecode_node e2 = bdecode({b2, sizeof(b2)-1}, ec);
	TEST_CHECK(!ec);

	std::string str1 = print_entry(e1);
	std::string str2 = print_entry(e2);
	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.type(), bdecode_node::int_t);
	std::printf("%s\n", print_entry(e1).c_str());

	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::int_t);
	TEST_EQUAL(e2.type(), bdecode_node::dict_t);
	TEST_EQUAL(print_entry(e1), str2);
	TEST_EQUAL(print_entry(e2), str1);
	std::printf("%s\n", print_entry(e1).c_str());

	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.type(), bdecode_node::int_t);
	TEST_EQUAL(print_entry(e1), str1);
	TEST_EQUAL(print_entry(e2), str2);
	std::printf("%s\n", print_entry(e1).c_str());
}

// test swap() (one node is the root of the other node)
TORRENT_TEST(swap_root)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";

	error_code ec;

	bdecode_node e1 = bdecode({b1, sizeof(b1)-1}, ec);
	TEST_CHECK(!ec);

	bdecode_node e2 = e1.dict_find("c").list_at(0);

	std::string str1 = print_entry(e1);
	std::string str2 = print_entry(e2);
	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.type(), bdecode_node::int_t);
	std::printf("%s\n", print_entry(e1).c_str());

	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::int_t);
	TEST_EQUAL(e2.type(), bdecode_node::dict_t);
	TEST_EQUAL(print_entry(e1), str2);
	TEST_EQUAL(print_entry(e2), str1);
	std::printf("%s\n", print_entry(e1).c_str());

	// swap back
	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.type(), bdecode_node::int_t);
	TEST_EQUAL(print_entry(e1), str1);
	TEST_EQUAL(print_entry(e2), str2);
	std::printf("%s\n", print_entry(e1).c_str());
}

// test swap() (neither is a root and they don't share a root)
TORRENT_TEST(swap_disjoint)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	char b2[] = "li1e3:fooli1ei2eed1:xi1eee";


	error_code ec;

	bdecode_node e1_root = bdecode({b1, sizeof(b1)-1}, ec);
	TEST_CHECK(!ec);
	bdecode_node e2_root = bdecode({b2, sizeof(b2)-1}, ec);
	TEST_CHECK(!ec);

	bdecode_node e1 = e1_root.dict_find("c").list_at(0);
	bdecode_node e2 = e2_root.list_at(1);

	std::string str1 = print_entry(e1);
	std::string str2 = print_entry(e2);
	TEST_EQUAL(e1.type(), bdecode_node::int_t);
	TEST_EQUAL(e2.type(), bdecode_node::string_t);

	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::string_t);
	TEST_EQUAL(e2.type(), bdecode_node::int_t);
	TEST_EQUAL(print_entry(e1), str2);
	TEST_EQUAL(print_entry(e2), str1);

	// swap back
	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::int_t);
	TEST_EQUAL(e2.type(), bdecode_node::string_t);
	TEST_EQUAL(print_entry(e1), str1);
	TEST_EQUAL(print_entry(e2), str2);
}

// test swap() (one is a root and they don't share a root)
TORRENT_TEST(swap_root_disjoint)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	char b2[] = "li1e3:fooli1ei2eed1:xi1eee";


	error_code ec;

	bdecode_node e1_root = bdecode({b1, sizeof(b1)-1}, ec);
	TEST_CHECK(!ec);
	bdecode_node e2 = bdecode({b2, sizeof(b2)-1}, ec);
	TEST_CHECK(!ec);

	bdecode_node e1 = e1_root.dict_find("d");

	std::string str1 = print_entry(e1);
	std::string str2 = print_entry(e2);
	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.type(), bdecode_node::list_t);

	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::list_t);
	TEST_EQUAL(e2.type(), bdecode_node::dict_t);
	TEST_EQUAL(print_entry(e1), str2);
	TEST_EQUAL(print_entry(e2), str1);

	// swap back
	e1.swap(e2);

	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.type(), bdecode_node::list_t);
	TEST_EQUAL(print_entry(e1), str1);
	TEST_EQUAL(print_entry(e2), str2);
}

// make sure it's safe to reuse bdecode_nodes after clear() is called
TORRENT_TEST(clear)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
	char b2[] = "li1ei2ee";

	error_code ec;
	bdecode_node e = bdecode({b1, sizeof(b1)-1}, ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(!ec);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);
	TEST_EQUAL(e.dict_size(), 4);
	TEST_EQUAL(e.dict_at(1).first, "b");

	e = bdecode({b2, sizeof(b2)-1}, ec);
	std::printf("%s\n", print_entry(e).c_str());
	TEST_CHECK(!ec);
	TEST_EQUAL(e.type(), bdecode_node::list_t);
	TEST_EQUAL(e.list_size(), 2);
	TEST_EQUAL(e.list_int_value_at(1), 2);
}

// assignment/copy of root nodes
TORRENT_TEST(copy_root)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";

	error_code ec;
	bdecode_node e1 = bdecode({b1, sizeof(b1)-1}, ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	std::printf("%s\n", print_entry(e1).c_str());

	bdecode_node e2(e1);
	bdecode_node e3;
	e3 = e1;

	e1.clear();

	TEST_EQUAL(e2.type(), bdecode_node::dict_t);
	TEST_EQUAL(e2.dict_size(), 4);
	TEST_EQUAL(e2.dict_at(1).first, "b");

	TEST_EQUAL(e3.type(), bdecode_node::dict_t);
	TEST_EQUAL(e3.dict_size(), 4);
	TEST_EQUAL(e3.dict_at(1).first, "b");
}

// non-owning references
TORRENT_TEST(non_owning_refs)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";

	error_code ec;
	bdecode_node e1 = bdecode({b1, sizeof(b1)-1}, ec);
	TEST_CHECK(!ec);

	TEST_EQUAL(e1.type(), bdecode_node::dict_t);
	std::printf("%s\n", print_entry(e1).c_str());

	bdecode_node e2 = e1.non_owning();

	TEST_EQUAL(e2.type(), bdecode_node::dict_t);

	e1.clear();

	// e2 is invalid now
}

// test that a partial parse can be still be printed up to the
// point where it faild
TORRENT_TEST(partial_parse)
{
	char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1-eee";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 35);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ], 'd': { 'x': {} } }");
}

TORRENT_TEST(partial_parse2)
{
	char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:d-d1:xi1eee";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 29);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ], 'd': {} }");
}

TORRENT_TEST(partial_parse3)
{
	char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee-1:dd1:xi1eee";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 26);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ] }");
}

TORRENT_TEST(partial_parse4)
{
	char b[] = "d1:ai1e1:b3:foo1:cli1e-i2ee1:dd1:xi1eee";

	error_code ec;
	int pos;
	bdecode_node e = bdecode(b, ec, &pos);
	TEST_EQUAL(pos, 22);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1 ] }");
}

TORRENT_TEST(partial_parse_string)
{
	// it's important to not have a null terminator here
	// to allow address sanitizer to trigger in case the decoder reads past the
	// end
	char b[] = { '5', '5'};

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b, sizeof(b)}, ec, &pos);
	TEST_CHECK(ec);
	TEST_EQUAL(pos, 2);
}

// test switch_underlying_buffer
TORRENT_TEST(switch_buffer)
{
	char b1[] = "d1:ai1e1:b3:foo1:cli1e-i2ee1:dd1:xi1eee";
	char b2[] = "d1:ai1e1:b3:foo1:cli1e-i2ee1:dd1:xi1eee";

	error_code ec;
	int pos;
	bdecode_node e = bdecode({b1, sizeof(b1)-1}, ec, &pos);
	TEST_EQUAL(pos, 22);
	TEST_EQUAL(e.type(), bdecode_node::dict_t);

	std::string string1 = print_entry(e);
	std::printf("%s\n", string1.c_str());

	e.switch_underlying_buffer(b2);

	std::string string2 = print_entry(e);
	std::printf("%s\n", string2.c_str());

	TEST_EQUAL(string1, string2);
}

TORRENT_TEST(long_string_99999999)
{
	std::string input;
	input += "99999999:";
	input.resize(9 + 99999999, '_');

	error_code ec;
	int pos;
	bdecode_node e = bdecode(input, ec, &pos);
	TEST_EQUAL(e.type(), bdecode_node::string_t);
	TEST_EQUAL(e.string_value(), input.substr(9));
}

TORRENT_TEST(long_string_100000000)
{
	std::string input;
	input += "100000000:";
	input.resize(10 + 100000000, '_');

	error_code ec;
	int pos;
	bdecode_node e = bdecode(input, ec, &pos);
	TEST_EQUAL(e.type(), bdecode_node::string_t);
	TEST_EQUAL(e.string_value(), input.substr(10));
}

TORRENT_TEST(data_offset)
{
	char const b[] = "li1e3:fooli1ei2eed1:xi1eee";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.data_offset(), 0);
	TEST_EQUAL(e.list_at(0).data_offset(), 1);
	TEST_EQUAL(e.list_at(1).data_offset(), 4);
	TEST_EQUAL(e.list_at(2).data_offset(), 9);
	TEST_EQUAL(e.list_at(3).data_offset(), 17);
}

TORRENT_TEST(string_offset)
{
	char const b[] = "l3:foo3:bare";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.list_at(0).string_offset(), 3);
	TEST_EQUAL(e.list_at(1).string_offset(), 8);
}

TORRENT_TEST(dict_at_node)
{
	char const b[] = "d3:foo3:bar4:test4:teste";
	error_code ec;
	bdecode_node e = bdecode(b, ec);
	TEST_CHECK(!ec);
	std::printf("%s\n", print_entry(e).c_str());

	TEST_EQUAL(e.dict_at_node(0).first.string_offset(), 3);
	TEST_EQUAL(e.dict_at_node(0).second.string_offset(), 8);
	TEST_EQUAL(e.dict_at_node(1).first.string_offset(), 13);
	TEST_EQUAL(e.dict_at_node(1).second.string_offset(), 19);
}
