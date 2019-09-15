/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"

#include <iostream>
#include <cstring>
#include <utility>

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/lazy_entry.hpp"
#endif

#include "test.hpp"

using namespace lt;

// test vectors from bittorrent protocol description
// http://www.bittorrent.com/protocol.html

namespace {

std::string encode(entry const& e)
{
	std::string ret;
	bencode(std::back_inserter(ret), e);
	return ret;
}

} // anonymous namespace

TORRENT_TEST(strings)
{
	entry e("spam");
	TEST_CHECK(encode(e) == "4:spam");
	TEST_CHECK(bdecode(encode(e)) == e);
}

TORRENT_TEST(integers)
{
	entry e(3);
	TEST_CHECK(encode(e) == "i3e");
	TEST_CHECK(bdecode(encode(e)) == e);
}

TORRENT_TEST(integers2)
{
	entry e(-3);
	TEST_CHECK(encode(e) == "i-3e");
	TEST_CHECK(bdecode(encode(e)) == e);
}

TORRENT_TEST(integers3)
{
	entry e(int(0));
	TEST_CHECK(encode(e) == "i0e");
	TEST_CHECK(bdecode(encode(e)) == e);
}

TORRENT_TEST(lists)
{
	entry::list_type l;
	l.push_back(entry("spam"));
	l.push_back(entry("eggs"));
	entry e(l);
	TEST_CHECK(encode(e) == "l4:spam4:eggse");
	TEST_CHECK(bdecode(encode(e)) == e);
}

TORRENT_TEST(dictionaries)
{
	entry e(entry::dictionary_t);
	e["spam"] = entry("eggs");
	e["cow"] = entry("moo");
	TEST_CHECK(encode(e) == "d3:cow3:moo4:spam4:eggse");
	TEST_CHECK(bdecode(encode(e)) == e);
}

TORRENT_TEST(preformatted)
{
	entry e(entry::preformatted_t);
	char const str[] = "foobar";
	e.preformatted().assign(str, str + sizeof(str)-1);
	TEST_EQUAL(encode(e), "foobar");
}

TORRENT_TEST(preformatted_node)
{
	entry e(entry::dictionary_t);
	char const str[] = "foobar";
	e["info"] = entry::preformatted_type(str, str + sizeof(str)-1);
	TEST_EQUAL(encode(e), "d4:infofoobare");
}

TORRENT_TEST(undefined_node)
{
	entry e(entry::undefined_t);
	TEST_EQUAL(encode(e), "0:");
}

TORRENT_TEST(undefined_node2)
{
	entry e(entry::dictionary_t);
	e["info"] = entry(entry::undefined_t);
	TEST_EQUAL(encode(e), "d4:info0:e");
}

TORRENT_TEST(implicit_construct)
{
	entry e(entry::list_t);
	e.list().push_back(entry::list_t);
	TEST_EQUAL(e.list().back().type(), entry::list_t);
}

TORRENT_TEST(print_dict_single_line)
{
	entry e;
	e["foo"] = "bar";
	e["bar"] = "foo";
	TEST_EQUAL(e.to_string(true), "{ 'bar': 'foo', 'foo': 'bar' }");
}

TORRENT_TEST(print_dict)
{
	entry e;
	e["foo"] = "bar";
	e["bar"] = "foo";
	TEST_EQUAL(e.to_string(), "{\n 'bar': 'foo',\n 'foo': 'bar' }");
}

TORRENT_TEST(print_list_single_line)
{
	entry e;
	e.list().push_back(entry("foo"));
	e.list().push_back(entry("bar"));
	TEST_EQUAL(e.to_string(true), "[ 'foo', 'bar' ]");
}


TORRENT_TEST(print_list)
{
	entry e;
	e.list().push_back(entry("foo"));
	e.list().push_back(entry("bar"));
	TEST_EQUAL(e.to_string(), "[\n 'foo',\n 'bar' ]");
}

TORRENT_TEST(print_int_single_line)
{
	entry e(1337);
	TEST_EQUAL(e.to_string(true), "1337");
}

TORRENT_TEST(print_int)
{
	entry e(1337);
	TEST_EQUAL(e.to_string(), "1337");
}

TORRENT_TEST(print_string_single_line)
{
	entry e("foobar");
	TEST_EQUAL(e.to_string(true), "'foobar'");
}

TORRENT_TEST(print_string)
{
	entry e("foobar");
	TEST_EQUAL(e.to_string(), "'foobar'");
}

TORRENT_TEST(print_deep_dict_single_line)
{
	entry e;
	e["strings"].list().push_back(entry("foo"));
	e["strings"].list().push_back(entry("bar"));
	e["ints"].list().push_back(entry(1));
	e["ints"].list().push_back(entry(2));
	e["ints"].list().push_back(entry(3));
	e["a"] = "foobar";
	TEST_EQUAL(e.to_string(true), "{ 'a': 'foobar', 'ints': [ 1, 2, 3 ], 'strings': [ 'foo', 'bar' ] }");
}

TORRENT_TEST(print_deep_dict)
{
	entry e;
	e["strings"].list().push_back(entry("foo"));
	e["strings"].list().push_back(entry("bar"));
	e["ints"].list().push_back(entry(1));
	e["ints"].list().push_back(entry(2));
	e["ints"].list().push_back(entry(3));
	e["a"] = "foobar";
	TEST_EQUAL(e.to_string(), "{\n 'a': 'foobar',\n 'ints': [\n   1,\n   2,\n   3 ],\n 'strings': [\n   'foo',\n   'bar' ] }");
}

TORRENT_TEST(integer_to_str)
{
	using lt::detail::integer_to_str;

	char buf[30];
	TEST_CHECK(integer_to_str(buf, 0) == "0"_sv);
	TEST_CHECK(integer_to_str(buf, 1234) == "1234"_sv);
	TEST_CHECK(integer_to_str(buf, -1234) == "-1234"_sv);
	TEST_CHECK(integer_to_str(buf, 123456789012345678LL) == "123456789012345678"_sv);
	TEST_CHECK(integer_to_str(buf, -123456789012345678LL) == "-123456789012345678"_sv);
}

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(lazy_entry)
{
	{
		char b[] = "i12453e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, std::size_t(section.second)) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == lazy_entry::int_t);
		TEST_CHECK(e.int_value() == 12453);
	}

	{
		char b[] = "26:abcdefghijklmnopqrstuvwxyz";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, std::size_t(section.second)) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == lazy_entry::string_t);
		TEST_CHECK(e.string_value() == std::string("abcdefghijklmnopqrstuvwxyz"));
		TEST_CHECK(e.string_length() == 26);
	}

	{
		char b[] = "li12453e3:aaae";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, std::size_t(section.second)) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == lazy_entry::list_t);
		TEST_CHECK(e.list_size() == 2);
		TEST_CHECK(e.list_at(0)->type() == lazy_entry::int_t);
		TEST_CHECK(e.list_at(1)->type() == lazy_entry::string_t);
		TEST_CHECK(e.list_at(0)->int_value() == 12453);
		TEST_CHECK(e.list_at(1)->string_value() == std::string("aaa"));
		TEST_CHECK(e.list_at(1)->string_length() == 3);
		section = e.list_at(1)->data_section();
		TEST_CHECK(std::memcmp("3:aaa", section.first, std::size_t(section.second)) == 0);
		TEST_CHECK(section.second == 5);
	}

	{
		char b[] = "d1:ai12453e1:b3:aaa1:c3:bbb1:X10:0123456789e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		std::pair<const char*, int> section = e.data_section();
		TEST_CHECK(std::memcmp(b, section.first, std::size_t(section.second)) == 0);
		TEST_CHECK(section.second == sizeof(b) - 1);
		TEST_CHECK(e.type() == lazy_entry::dict_t);
		TEST_CHECK(e.dict_size() == 4);
		TEST_CHECK(e.dict_find("a")->type() == lazy_entry::int_t);
		TEST_CHECK(e.dict_find("a")->int_value() == 12453);
		TEST_CHECK(e.dict_find("b")->type() == lazy_entry::string_t);
		TEST_CHECK(e.dict_find("b")->string_value() == std::string("aaa"));
		TEST_CHECK(e.dict_find("b")->string_length() == 3);
		TEST_CHECK(e.dict_find("c")->type() == lazy_entry::string_t);
		TEST_CHECK(e.dict_find("c")->string_value() == std::string("bbb"));
		TEST_CHECK(e.dict_find("c")->string_length() == 3);
		TEST_CHECK(e.dict_find_string_value("X") == "0123456789");
	}

	// dictionary key with \0
	{
		char b[] = "d3:a\0bi1ee";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		TEST_CHECK(e.dict_size() == 1);
		lazy_entry* d = e.dict_find({"a\0b", 3});
		TEST_CHECK(d);
		TEST_EQUAL(d->type(), lazy_entry::int_t);
		TEST_EQUAL(d->int_value(), 1);
	}

	// test strings with negative length-prefix
	{
		char b[] = "-10:foobar";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_value));
	}

	// test strings with overflow length-prefix
	{
		char b[] = "18446744073709551615:foobar";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::overflow));
	}

	// test integers that don't fit in 64 bits
	{
		char b[] = "i18446744073709551615e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		// the lazy aspect makes this overflow when asking for
		// the value. turning it to zero.
		TEST_CHECK(e.int_value() == 0);
	}

	// test integers that just exactly fit in 64 bits
	{
		char b[] = "i9223372036854775807e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_CHECK(e.int_value() == 9223372036854775807LL);
	}

	// test integers that just exactly fit in 64 bits
	{
		char b[] = "i-9223372036854775807e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TEST_CHECK(ret == 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_CHECK(e.int_value() == -9223372036854775807LL);
	}

	// test invalid encoding
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
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(reinterpret_cast<char*>(buf), reinterpret_cast<char*>(buf) + sizeof(buf), e, ec);
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

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b), e, ec);
		TEST_CHECK(ret != 0);
		TEST_EQUAL(ec, error_code(bdecode_errors::depth_exceeded));
	}

	// test the item limit
	{
		char b[10240];
		b[0] = 'l';
		int i = 1;
		for (i = 1; i < 10239; i += 2)
			memcpy(&b[i], "0:", 2);
		b[i] = 'e';

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + i + 1, e, ec, nullptr, 1000, 1000);
		TEST_CHECK(ret != 0);
		TEST_EQUAL(ec, error_code(bdecode_errors::limit_exceeded));
	}

	// test unexpected EOF
	{
		char b[] = "l2:.."; // expected terminating 'e'

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}

	// test unexpected EOF (really expected terminator)
	{
		char b[] = "l2:..0"; // expected terminating 'e' instead of '0'

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}

	// test expected string
	{
		char b[] = "di2ei0ee";
		// expected string (dict keys must be strings)

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	}

	// test unexpected EOF while parsing dict key
	{
		char b[] = "d1000:..e";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}

	// test unexpected EOF while parsing dict key
	{
		char b[] = "d1000:";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}

	// test expected string while parsing dict key
	{
		char b[] = "df00:";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_digit));
	}

	// test unexpected EOF while parsing int
	{
		char b[] = "i";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}

	// test unexpected EOF while parsing int
	{
		char b[] = "i10";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}


	// test expected colon
	{
		char b[] = "d1000";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::expected_colon));
	}

	// test empty string
	{
		char b[] = "";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_EQUAL(ret, -1);
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
		std::printf("%s\n", print_entry(e).c_str());
	}

	// test partial string
	{
		char b[] = "100:..";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_CHECK(ret != 0);
		std::printf("%s\n", print_entry(e).c_str());
		TEST_EQUAL(ec, error_code(bdecode_errors::unexpected_eof));
	}

	// test pascal string dict
	{
		char b[] = "d6:foobar6:barfooe";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_EQUAL(ret, 0);
		std::printf("%s\n", print_entry(e).c_str());

		pascal_string ps = e.dict_find_pstr("foobar");
		TEST_EQUAL(std::memcmp(ps.ptr, "barfoo", std::size_t(ps.len)), 0);
		TEST_EQUAL(ps.len, 6);

		ps = e.dict_find_pstr("foobar2");
		TEST_EQUAL(ps.ptr, static_cast<char const*>(nullptr));
		TEST_EQUAL(ps.len, 0);
	}

	// test pascal string in list
	{
		char b[] = "l6:foobari4ee";

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec, nullptr);
		TEST_EQUAL(ret, 0);
		std::printf("%s\n", print_entry(e).c_str());

		TEST_EQUAL(e.list_size(), 2);
		pascal_string ps = e.list_pstr_at(0);
		TEST_EQUAL(std::memcmp(ps.ptr, "foobar", std::size_t(ps.len)), 0);
		TEST_EQUAL(ps.len, 6);

		ps = e.list_pstr_at(1);
		TEST_EQUAL(ps.ptr, static_cast<char const*>(nullptr));
		TEST_EQUAL(ps.len, 0);
	}

	{
		unsigned char buf[] = { 0x44, 0x91, 0x3a };
		error_code ec;
		entry ent = bdecode({reinterpret_cast<char*>(buf), int(sizeof(buf))}, ec);
		TEST_CHECK(ent == entry());
	}

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

		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(buf.data(), buf.data() + buf.size(), e, ec);
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e.type(), lazy_entry::list_t);
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
			std::snprintf(tmp, sizeof(tmp), "4:%04di%de", i, i);
			buf += tmp;
		}
		buf += "e";

		std::printf("%s\n", buf.c_str());
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(buf.data(), buf.data() + buf.size(), e, ec);
		TEST_EQUAL(ret, 0);
		TEST_EQUAL(e.type(), lazy_entry::dict_t);
		TEST_EQUAL(e.dict_size(), 1000);
		for (int i = 0; i < 1000; ++i)
		{
			char tmp[30];
			std::snprintf(tmp, sizeof(tmp), "%04d", i);
			TEST_EQUAL(e.dict_find_int_value(tmp), i);
		}
	}

	// test parse_int
	{
		char b[] = "1234567890e";
		std::int64_t val = 0;
		bdecode_errors::error_code_enum ec = bdecode_errors::no_error;
		char const* e = parse_int(b, b + sizeof(b)-1, 'e', val, ec);
		TEST_CHECK(ec == bdecode_errors::no_error);
		TEST_EQUAL(val, 1234567890);
		TEST_EQUAL(e, b + sizeof(b) - 2);
	}

	// test invalid digit
	{
		char b[] = "0o";
		std::int64_t val = 0;
		bdecode_errors::error_code_enum ec;
		char const* e = parse_int(b, b + sizeof(b)-1, 'e', val, ec);
		TEST_EQUAL(ec, bdecode_errors::expected_digit);
		TEST_EQUAL(e, b + 1);
	}

	{
		char b[] = "9223372036854775808:";
		std::int64_t val = 0;
		bdecode_errors::error_code_enum ec;
		char const* e = parse_int(b, b + sizeof(b)-1, ':', val, ec);
		TEST_CHECK(ec == bdecode_errors::overflow);
		TEST_EQUAL(e, b + 18);
	}

	{
		char b[] = "928";
		std::int64_t val = 0;
		bdecode_errors::error_code_enum ec = bdecode_errors::no_error;
		char const* e = parse_int(b, b + sizeof(b)-1, ':', val, ec);
		TEST_CHECK(ec == bdecode_errors::no_error);
		TEST_EQUAL(e, b + 3);
	}

	{
		char const* b[] = {
			"d1:a1919191010:11111",
			"d2143289344:a4:aaaae",
			"d214328934114:a4:aaaae",
			"d9205357638345293824:a4:aaaae",
			"d1:a9205357638345293824:11111",
		};

		for (int i = 0; i < int(sizeof(b)/sizeof(b[0])); ++i)
		{
			lazy_entry tmp;
			error_code ec;
			int ret = lazy_bdecode(b[i], b[i] + strlen(b[i]), tmp, ec, nullptr);
			lazy_entry e;
			e = std::move(tmp);
			TEST_EQUAL(ret, -1);
			TEST_CHECK(ec == error_code(bdecode_errors::unexpected_eof));
			std::printf("%s\n", print_entry(e).c_str());

			lazy_entry* moved = new lazy_entry(std::move(e));
			delete moved;
		}
	}
}
#endif // TORRENT_ABI_VERSION
