/*

Copyright (c) 2015, Arvid Norberg
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

using namespace libtorrent;

int test_main()
{
	// test integer
	{
		char b[] = "i12453e";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, section.second) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == bdecode_node::int_t);
		TEST_CHECK(e.int_value() == 12453);
	}
	
	// test string
	{
		char b[] = "26:abcdefghijklmnopqrstuvwxyz";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, section.second) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == bdecode_node::string_t);
		TEST_CHECK(e.string_value() == std::string("abcdefghijklmnopqrstuvwxyz"));
		TEST_CHECK(e.string_length() == 26);
	}

	// test list
	{
		char b[] = "li12453e3:aaae";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, section.second) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == bdecode_node::list_t);
		TEST_CHECK(e.list_size() == 2);
		TEST_CHECK(e.list_at(0).type() == bdecode_node::int_t);
		TEST_CHECK(e.list_at(1).type() == bdecode_node::string_t);
		TEST_CHECK(e.list_at(0).int_value() == 12453);
		TEST_CHECK(e.list_at(1).string_value() == std::string("aaa"));
		TEST_CHECK(e.list_at(1).string_length() == 3);
		section = e.list_at(1).data_section();
		TEST_CHECK(std::memcmp("3:aaa", section.first, section.second) == 0);
		TEST_CHECK(section.second == 5);
	}

	// test dict
	{
		char b[] = "d1:ai12453e1:b3:aaa1:c3:bbb1:X10:0123456789e";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, section.second) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == bdecode_node::dict_t);
		TEST_CHECK(e.dict_size() == 4);
		TEST_CHECK(e.dict_find("a").type() == bdecode_node::int_t);
		TEST_CHECK(e.dict_find("a").int_value() == 12453);
		TEST_CHECK(e.dict_find("b").type() == bdecode_node::string_t);
		TEST_CHECK(e.dict_find("b").string_value() == std::string("aaa"));
		TEST_CHECK(e.dict_find("b").string_length() == 3);
		TEST_CHECK(e.dict_find("c").type() == bdecode_node::string_t);
		TEST_CHECK(e.dict_find("c").string_value() == std::string("bbb"));
		TEST_CHECK(e.dict_find("c").string_length() == 3);
		TEST_CHECK(e.dict_find_string_value("X") == "0123456789");
	}

	// test dictionary with a key without a value
	{
		char b[] = "d1:ai1e1:be";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 10);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_value));
		printf("%s\n", print_entry(e).c_str());
	}

	// test dictionary with a key that's not a string
	{
		char b[] = "di5e1:ae";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 1);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}

	// dictionary key with \0
	{
		char b[] = "d3:a\0bi1ee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		TEST_CHECK(e.dict_size() == 1);
		bdecode_node d = e.dict_find(std::string("a\0b", 3));
		TEST_EQUAL(d.type(), bdecode_node::int_t);
		TEST_EQUAL(d.int_value(), 1);
	}

	// premature e
	{
		char b[] = "e";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test strings with negative length-prefix
	{
		char b[] = "-10:foobar";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 0);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_value));
		printf("%s\n", print_entry(e).c_str());
	}

	// test strings with overflow length-prefix
	{
		char b[] = "18446744073709551615:foobar";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 19);
		TEST_EQUAL(ec,  error_code(bdecode_errors::overflow));
		printf("%s\n", print_entry(e).c_str());
	}

	// test strings with almost overflow (more than 8 digits)
	{
		char b[] = "99999999:foobar";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 8);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test strings with overflow (more than 8 digits)
	{
		char b[] = "199999999:foobar";
		bdecode_node e;
		error_code ec;
		int pos;
		// pretend that we have a large buffer like that
		int ret = bdecode(b, b + 999999999, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 0);
		TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded));
		printf("%s\n", print_entry(e).c_str());
	}

	// test integer without any digits
	{
		char b[] = "ie";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 1);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}

	// test integer with just a minus
	{
		char b[] = "i-e";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 2);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}


	// test integer with a minus inserted in it
	{
		char b[] = "i35412-5633e";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 6);
		TEST_EQUAL(ec,  error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}


	// test integers that don't fit in 64 bits
	{
		char b[] = "i18446744073709551615e";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());
		// the lazy aspect makes this overflow when asking for
		// the value. turning it to zero.
		TEST_EQUAL(e.int_value(), 0);
	}

	// test integers with more than 20 digits (overflow on parsing)
	{
		char b[] = "i184467440737095516154e";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 22);
		TEST_EQUAL(ec,  error_code(bdecode_errors::overflow));
		printf("%s\n", print_entry(e).c_str());
	}

	// test truncated negative integer
	{
		char b[] = "i-";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 2);
		TEST_EQUAL(ec,  error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test truncated negative integer
	{
		char b[] = "i-e";
		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 2);
		TEST_EQUAL(ec,  error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}

	// bdecode_error
	{
		error_code ec(bdecode_errors::overflow);
		TEST_EQUAL(ec.message(), "integer overflow");
		TEST_EQUAL(ec.category().name(), std::string("bdecode error"));
		ec.assign(5434, get_bdecode_category());
		TEST_EQUAL(ec.message(), "Unknown error");
	}

	// test integers that just exactly fit in 64 bits
	{
		char b[] = "i9223372036854775807e";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		printf("%s\n", print_entry(e).c_str());
		TEST_CHECK(e.int_value() == 9223372036854775807LL);
	}

	// test integers that just exactly fit in 64 bits
	{
		char b[] = "i-9223372036854775807e";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		printf("%s\n", print_entry(e).c_str());
		TEST_CHECK(e.int_value() == -9223372036854775807LL);
	}

	// test integers that have invalid digits
	{
		char b[] = "i92337203t854775807e";
		bdecode_node e;
		error_code ec;
		int pos = 0;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 9);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}

	// test invalid encoding
	{
		unsigned char buf[] =
			{ 0x64	, 0x31	, 0x3a	, 0x61	, 0x64	, 0x32	, 0x3a	, 0x69
			, 0x64	, 0x32	, 0x30	, 0x3a	, 0x2a	, 0x21	, 0x19	, 0x89
			, 0x9f	, 0xcd	, 0x5f	, 0xc9	, 0xbc	, 0x80	, 0xc1	, 0x76
			, 0xfe	, 0xe0	, 0xc6	, 0x84	, 0x2d	, 0xf6	, 0xfc	, 0xb8
			, 0x39	, 0x3a	, 0x69	, 0x6e	, 0x66	, 0x6f	, 0x5f	, 0x68
			, 0x61	, 0xae	, 0x68	, 0x32	, 0x30	, 0x3a	, 0x14	, 0x78
			, 0xd5	, 0xb0	, 0xdc	, 0xf6	, 0x82	, 0x42	, 0x32	, 0xa0
			, 0xd6	, 0x88	, 0xeb	, 0x48	, 0x57	, 0x01	, 0x89	, 0x40
			, 0x4e	, 0xbc	, 0x65	, 0x31	, 0x3a	, 0x71	, 0x39	, 0x3a
			, 0x67	, 0x65	, 0x74	, 0x5f	, 0x70	, 0x65	, 0x65	, 0x72
			, 0x78	, 0xff	, 0x3a	, 0x74	, 0x38	, 0x3a	, 0xaa	, 0xd4
			, 0xa1	, 0x88	, 0x7a	, 0x8d	, 0xc3	, 0xd6	, 0x31	, 0x3a
			, 0x79	, 0x31	, 0xae	, 0x71	, 0x65	, 0};

		printf("%s\n", buf);
		bdecode_node e;
		error_code ec;
		int ret = bdecode((char*)buf, (char*)buf + sizeof(buf), e, ec);
		TEST_CHECK(ret == -1);	
	}

	// test the depth limit
	{
		char b[2048];
		for (int i = 0; i < 1024; ++i)
			b[i]= 'l';

		for (int i = 1024; i < 2048; ++i)
			b[i]= 'e';

		// 1024 levels nested lists

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b), e, ec, NULL, 100);
		TEST_CHECK(ret != 0);
		TEST_EQUAL(ec, error_code(bdecode_errors::depth_exceeded
			, get_bdecode_category()));
	}

	// test the item limit
	{
		char b[10240];
		b[0] = 'l';
		int i = 1;
		for (i = 1; i < 10239; i += 2)
			memcpy(&b[i], "0:", 2);
		b[i] = 'e';

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + i + 1, e, ec, NULL, 1000, 1000);
		TEST_CHECK(ret != 0);
		TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded
			, get_bdecode_category()));
	}

	// test unexpected EOF
	{
		char b[] = "l2:.."; // expected terminating 'e'

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 5);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test unexpected EOF (really expected terminator)
	{
		char b[] = "l2:..0"; // expected terminating 'e' instead of '0'

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 6);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_colon));
		printf("%s\n", print_entry(e).c_str());
	}

	// test expected string
	{
		char b[] = "di2ei0ee";
		// expected string (dict keys must be strings)

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 1);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}

	// test unexpected EOF while parsing dict key
	{
		char b[] = "d1000:..e";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 5);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test unexpected EOF while parsing dict key
	{
		char b[] = "d1000:";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 5);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test expected string while parsing dict key
	{
		char b[] = "df00:";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 1);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
		printf("%s\n", print_entry(e).c_str());
	}

	// test unexpected EOF while parsing int
	{
		char b[] = "i";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 1);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	// test unexpected EOF while parsing int
	{
		char b[] = "i10";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 3);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}


	// test expected colon
	{
		char b[] = "d1000";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 5);
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_colon));
		printf("%s\n", print_entry(e).c_str());
	}

	// test empty string
	{
		char b[] = "";

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, NULL);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());
	}

	// test partial string
	{
		char b[] = "100:..";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 3);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		printf("%s\n", print_entry(e).c_str());
	}

	{
		std::string buf;
		buf += "l";
		for (int i = 0; i < 1000; ++i)
		{
			char tmp[20];
			snprintf(tmp, sizeof(tmp), "i%de", i);
			buf += tmp;
		}
		buf += "e";

		bdecode_node e;
		error_code ec;
		int ret = bdecode((char*)&buf[0], (char*)&buf[0] + buf.size(), e, ec);
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e.type(), bdecode_node::list_t);
		TEST_EQUAL(e.list_size(), 1000);
		for (int i = 0; i < 1000; ++i)
		{
			TEST_EQUAL(e.list_int_value_at(i), i);
		}
	}

	{
		std::string buf;
		buf += "d";
		for (int i = 0; i < 1000; ++i)
		{
			char tmp[30];
			snprintf(tmp, sizeof(tmp), "4:%04di%de", i, i);
			buf += tmp;
		}
		buf += "e";

		printf("%s\n", buf.c_str());
		bdecode_node e;
		error_code ec;
		int ret = bdecode((char*)&buf[0], (char*)&buf[0] + buf.size(), e, ec);
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e.type(), bdecode_node::dict_t);
		TEST_EQUAL(e.dict_size(), 1000);
		for (int i = 0; i < 1000; ++i)
		{
			char tmp[30];
			snprintf(tmp, sizeof(tmp), "%04d", i);
			TEST_EQUAL(e.dict_find_int_value(tmp), i);
		}
	}

	// test dict_at
	{
		char b[] = "d3:fooi1e3:bari2ee";

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);

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
	{
		char b[] = "l3:fooe";

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
	
		TEST_EQUAL(e.type(), bdecode_node::list_t);
		TEST_EQUAL(e.list_size(), 1);
		TEST_EQUAL(e.list_at(0).type(), bdecode_node::string_t);
		TEST_EQUAL(e.list_at(0).string_ptr(), b + 3);
		TEST_EQUAL(e.list_at(0).string_length(), 3);
	}

	// test exceeding buffer size limit
	{
		char b[] = "l3:fooe";

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + 0x3fffffff, e, ec);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded));
		printf("%s\n", print_entry(e).c_str());
	}

	// test parse_int
	{
		char b[] = "1234567890e";
		boost::int64_t val = 0;
		bdecode_errors::error_code_enum ec;
		char const* e = parse_int(b, b + sizeof(b)-1, 'e', val, ec);
		TEST_EQUAL(val, 1234567890);
		TEST_EQUAL(e, b + sizeof(b) - 2);
	}

	// test invalid digit
	{
		char b[] = "0o";
		boost::int64_t val = 0;
		bdecode_errors::error_code_enum ec;
		char const* e = parse_int(b, b + sizeof(b)-1, 'e', val, ec);
		TEST_EQUAL(ec, bdecode_errors::expected_digit);
		TEST_EQUAL(e, b + 1);
	}

	// test parse_int overflow
	{
		char b[] = "9223372036854775808:";
		boost::int64_t val = 0;
		bdecode_errors::error_code_enum ec;
		char const* e = parse_int(b, b + sizeof(b)-1, ':', val, ec);
		TEST_EQUAL(ec, bdecode_errors::overflow);
		TEST_EQUAL(e, b + 18);
	}

	{
		char b[] = "928";
		boost::int64_t val = 0;
		bdecode_errors::error_code_enum ec;
		char const* e = parse_int(b, b + sizeof(b)-1, ':', val, ec);
		TEST_EQUAL(ec, bdecode_errors::expected_colon);
		TEST_EQUAL(e, b + 3);
	}

	// test dict_find_* functions
	{
		// a: int
		// b: string
		// c: list
		// d: dict
		char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());

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
	{
		// int
		// string
		// list
		// dict
		char b[] = "li1e3:fooli1ei2eed1:xi1eee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());

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
	{
		// int
		// string
		// list
		// dict
		char b[] = "li1e3:fooli1ei2eed1:xi1eee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(e.type(), bdecode_node::list_t);

		TEST_EQUAL(e.list_at(3).type(), bdecode_node::dict_t);
		TEST_EQUAL(e.list_at(2).type(), bdecode_node::list_t);
		TEST_EQUAL(e.list_string_value_at(1), "foo");
		TEST_EQUAL(e.list_int_value_at(0), 1);

		TEST_EQUAL(e.list_size(), 4);
		TEST_EQUAL(e.list_size(), 4);
	}


	// test dict_find_* functions
	{
		// a: int
		// b: string
		// c: list
		// d: dict
		char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(e.type(), bdecode_node::dict_t);

		// try finding the last item in a dict (to skip all the other ones)
		TEST_EQUAL(e.dict_find("d").type(), bdecode_node::dict_t);
		TEST_EQUAL(e.dict_find(std::string("d")).type(), bdecode_node::dict_t);
	}

	// print_entry

	{
		char b[] = "li1e3:fooli1ei2eed1:xi1eee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());
	
		TEST_EQUAL(print_entry(e), "[ 1, 'foo', [ 1, 2 ], { 'x': 1 } ]");
	}

	{
		char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		bdecode_node e;
		error_code ec;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_EQUAL(ret, 0);
		printf("%s\n", print_entry(e).c_str());
	
		TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ], 'd': { 'x': 1 } }");
	}

	// test swap()
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		char b2[] = "i1e";

		bdecode_node e1;
		bdecode_node e2;

		error_code ec;
	
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e1, ec);
		TEST_EQUAL(ret, 0);
		ret = bdecode(b2, b2 + sizeof(b2)-1, e2, ec);
		TEST_EQUAL(ret, 0);

		std::string str1 = print_entry(e1);
		std::string str2 = print_entry(e2);
		TEST_EQUAL(e1.type(), bdecode_node::dict_t);
		TEST_EQUAL(e2.type(), bdecode_node::int_t);
		printf("%s\n", print_entry(e1).c_str());

		e1.swap(e2);

		TEST_EQUAL(e1.type(), bdecode_node::int_t);
		TEST_EQUAL(e2.type(), bdecode_node::dict_t);
		TEST_EQUAL(print_entry(e1), str2);
		TEST_EQUAL(print_entry(e2), str1);
		printf("%s\n", print_entry(e1).c_str());

		e1.swap(e2);

		TEST_EQUAL(e1.type(), bdecode_node::dict_t);
		TEST_EQUAL(e2.type(), bdecode_node::int_t);
		TEST_EQUAL(print_entry(e1), str1);
		TEST_EQUAL(print_entry(e2), str2);
		printf("%s\n", print_entry(e1).c_str());
	}

	// test swap() (one node is the root of the other node)
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";

		bdecode_node e1;
		bdecode_node e2;

		error_code ec;
	
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e1, ec);
		TEST_EQUAL(ret, 0);

		e2 = e1.dict_find("c").list_at(0);

		std::string str1 = print_entry(e1);
		std::string str2 = print_entry(e2);
		TEST_EQUAL(e1.type(), bdecode_node::dict_t);
		TEST_EQUAL(e2.type(), bdecode_node::int_t);
		printf("%s\n", print_entry(e1).c_str());

		e1.swap(e2);

		TEST_EQUAL(e1.type(), bdecode_node::int_t);
		TEST_EQUAL(e2.type(), bdecode_node::dict_t);
		TEST_EQUAL(print_entry(e1), str2);
		TEST_EQUAL(print_entry(e2), str1);
		printf("%s\n", print_entry(e1).c_str());

		// swap back
		e1.swap(e2);

		TEST_EQUAL(e1.type(), bdecode_node::dict_t);
		TEST_EQUAL(e2.type(), bdecode_node::int_t);
		TEST_EQUAL(print_entry(e1), str1);
		TEST_EQUAL(print_entry(e2), str2);
		printf("%s\n", print_entry(e1).c_str());
	}

	// test swap() (neither is a root and they don't share a root)
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		char b2[] = "li1e3:fooli1ei2eed1:xi1eee";

		bdecode_node e1_root;
		bdecode_node e2_root;

		error_code ec;
	
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e1_root, ec);
		TEST_EQUAL(ret, 0);
		ret = bdecode(b2, b2 + sizeof(b2)-1, e2_root, ec);
		TEST_EQUAL(ret, 0);

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
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		char b2[] = "li1e3:fooli1ei2eed1:xi1eee";

		bdecode_node e1_root;
		bdecode_node e2;

		error_code ec;
	
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e1_root, ec);
		TEST_EQUAL(ret, 0);
		ret = bdecode(b2, b2 + sizeof(b2)-1, e2, ec);
		TEST_EQUAL(ret, 0);

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
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";
		char b2[] = "li1ei2ee";

		bdecode_node e;
		error_code ec;
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e, ec);
		printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e.type(), bdecode_node::dict_t);
		TEST_EQUAL(e.dict_size(), 4);
		TEST_EQUAL(e.dict_at(1).first, "b");

		ret = bdecode(b2, b2 + sizeof(b2)-1, e, ec);
		printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e.type(), bdecode_node::list_t);
		TEST_EQUAL(e.list_size(), 2);
		TEST_EQUAL(e.list_int_value_at(1), 2);
	}

	// assignment/copy of root nodes
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";

		bdecode_node e1;
		error_code ec;
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e1, ec);
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e1.type(), bdecode_node::dict_t);
		printf("%s\n", print_entry(e1).c_str());

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
	{
		char b1[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1eee";

		bdecode_node e1;
		error_code ec;
		int ret = bdecode(b1, b1 + sizeof(b1)-1, e1, ec);

		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e1.type(), bdecode_node::dict_t);
		printf("%s\n", print_entry(e1).c_str());

		bdecode_node e2 = e1.non_owning();

		TEST_EQUAL(e2.type(), bdecode_node::dict_t);

		e1.clear();

		// e2 is invalid now
	}

	// test that a partial parse can be still be printed up to the
	// point where it faild
	{
		char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:dd1:xi1-eee";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 35);
		TEST_EQUAL(e.type(), bdecode_node::dict_t);

		printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ], 'd': { 'x': {} } }");
	}
	{
		char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee1:d-d1:xi1eee";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 29);
		TEST_EQUAL(e.type(), bdecode_node::dict_t);

		printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ], 'd': {} }");
	}
	{
		char b[] = "d1:ai1e1:b3:foo1:cli1ei2ee-1:dd1:xi1eee";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 26);
		TEST_EQUAL(e.type(), bdecode_node::dict_t);

		printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1, 2 ] }");
	}
	{
		char b[] = "d1:ai1e1:b3:foo1:cli1e-i2ee1:dd1:xi1eee";

		bdecode_node e;
		error_code ec;
		int pos;
		int ret = bdecode(b, b + sizeof(b)-1, e, ec, &pos);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(pos, 22);
		TEST_EQUAL(e.type(), bdecode_node::dict_t);

		printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(print_entry(e), "{ 'a': 1, 'b': 'foo', 'c': [ 1 ] }");
	}

	// TODO: test switch_underlying_buffer

	return 0;
}

