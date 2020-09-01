/*

Copyright (c) 2005, 2008, 2015-2020, Arvid Norberg
Copyright (c) 2018, Eugene Shalygin
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

#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"

#include <iostream>
#include <cstring>
#include <utility>

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

TORRENT_TEST(dict_constructor)
{
	entry::dictionary_type e{{std::string("foo"), std::string("bar")},
		{std::string("bar"), 1234}};

	TEST_EQUAL(entry(e).to_string(), "{\n 'bar': 1234,\n 'foo': 'bar' }");
}

TORRENT_TEST(integer_to_str)
{
	using lt::aux::integer_to_str;

	std::array<char, 21> buf;
	TEST_CHECK(integer_to_str(buf, 0) == "0"_sv);
	TEST_CHECK(integer_to_str(buf, 1) == "1"_sv);
	TEST_CHECK(integer_to_str(buf, 2) == "2"_sv);
	TEST_CHECK(integer_to_str(buf, 3) == "3"_sv);
	TEST_CHECK(integer_to_str(buf, 4) == "4"_sv);
	TEST_CHECK(integer_to_str(buf, 5) == "5"_sv);
	TEST_CHECK(integer_to_str(buf, 6) == "6"_sv);
	TEST_CHECK(integer_to_str(buf, 7) == "7"_sv);
	TEST_CHECK(integer_to_str(buf, 8) == "8"_sv);
	TEST_CHECK(integer_to_str(buf, 9) == "9"_sv);
	TEST_CHECK(integer_to_str(buf, 10) == "10"_sv);
	TEST_CHECK(integer_to_str(buf, 11) == "11"_sv);
	TEST_CHECK(integer_to_str(buf, -1) == "-1"_sv);
	TEST_CHECK(integer_to_str(buf, -2) == "-2"_sv);
	TEST_CHECK(integer_to_str(buf, -3) == "-3"_sv);
	TEST_CHECK(integer_to_str(buf, -4) == "-4"_sv);
	TEST_CHECK(integer_to_str(buf, -5) == "-5"_sv);
	TEST_CHECK(integer_to_str(buf, -6) == "-6"_sv);
	TEST_CHECK(integer_to_str(buf, -7) == "-7"_sv);
	TEST_CHECK(integer_to_str(buf, -8) == "-8"_sv);
	TEST_CHECK(integer_to_str(buf, -9) == "-9"_sv);
	TEST_CHECK(integer_to_str(buf, -10) == "-10"_sv);
	TEST_CHECK(integer_to_str(buf, -11) == "-11"_sv);
	TEST_CHECK(integer_to_str(buf, 12) == "12"_sv);
	TEST_CHECK(integer_to_str(buf, -12) == "-12"_sv);
	TEST_CHECK(integer_to_str(buf, 123) == "123"_sv);
	TEST_CHECK(integer_to_str(buf, -123) == "-123"_sv);
	TEST_CHECK(integer_to_str(buf, 1234) == "1234"_sv);
	TEST_CHECK(integer_to_str(buf, -1234) == "-1234"_sv);
	TEST_CHECK(integer_to_str(buf, 12345) == "12345"_sv);
	TEST_CHECK(integer_to_str(buf, -12345) == "-12345"_sv);
	TEST_CHECK(integer_to_str(buf, 123456) == "123456"_sv);
	TEST_CHECK(integer_to_str(buf, -123456) == "-123456"_sv);
	TEST_CHECK(integer_to_str(buf, 123456789012345678LL) == "123456789012345678"_sv);
	TEST_CHECK(integer_to_str(buf, -123456789012345678LL) == "-123456789012345678"_sv);
	TEST_CHECK(integer_to_str(buf, std::numeric_limits<std::int64_t>::max()) == "9223372036854775807"_sv);
	TEST_CHECK(integer_to_str(buf, std::numeric_limits<std::int64_t>::min()) == "-9223372036854775808"_sv);
}
