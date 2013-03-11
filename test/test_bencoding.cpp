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
#include "libtorrent/lazy_entry.hpp"
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <cstring>

#include "test.hpp"

using namespace libtorrent;

// test vectors from bittorrent protocol description
// http://www.bittorrent.com/protocol.html

std::string encode(entry const& e)
{
	std::string ret;
	bencode(std::back_inserter(ret), e);
	return ret;
}

entry decode(std::string const& str)
{
	return bdecode(str.begin(), str.end());
}

int test_main()
{
	using namespace libtorrent;

	// ** strings **
	{	
		entry e("spam");
		TEST_CHECK(encode(e) == "4:spam");
		TEST_CHECK(decode(encode(e)) == e);
	}

	// ** integers **
	{
		entry e(3);
		TEST_CHECK(encode(e) == "i3e");
		TEST_CHECK(decode(encode(e)) == e);
	}

	{
		entry e(-3);
		TEST_CHECK(encode(e) == "i-3e");
		TEST_CHECK(decode(encode(e)) == e);
	}

	{
		entry e(int(0));
		TEST_CHECK(encode(e) == "i0e");
		TEST_CHECK(decode(encode(e)) == e);
	}

	// ** lists **
	{
		entry::list_type l;
		l.push_back(entry("spam"));
		l.push_back(entry("eggs"));
		entry e(l);
		TEST_CHECK(encode(e) == "l4:spam4:eggse");
		TEST_CHECK(decode(encode(e)) == e);
	}

	// ** dictionaries **
	{
		entry e(entry::dictionary_t);
		e["spam"] = entry("eggs");
		e["cow"] = entry("moo");
		TEST_CHECK(encode(e) == "d3:cow3:moo4:spam4:eggse");
		TEST_CHECK(decode(encode(e)) == e);
	}

	{
		char b[] = "i12453e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TORRENT_ASSERT(ret == 0);
#if TORRENT_USE_IOSTREAM
		std::cout << e << std::endl;
#endif
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(std::memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::int_t);
		TORRENT_ASSERT(e.int_value() == 12453);
	}
	
	{
		char b[] = "26:abcdefghijklmnopqrstuvwxyz";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TORRENT_ASSERT(ret == 0);
#if TORRENT_USE_IOSTREAM
		std::cout << e << std::endl;
#endif
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(std::memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.string_value() == std::string("abcdefghijklmnopqrstuvwxyz"));
		TORRENT_ASSERT(e.string_length() == 26);
	}

	{
		char b[] = "li12453e3:aaae";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TORRENT_ASSERT(ret == 0);
#if TORRENT_USE_IOSTREAM
		std::cout << e << std::endl;
#endif
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(std::memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::list_t);
		TORRENT_ASSERT(e.list_size() == 2);
		TORRENT_ASSERT(e.list_at(0)->type() == lazy_entry::int_t);
		TORRENT_ASSERT(e.list_at(1)->type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.list_at(0)->int_value() == 12453);
		TORRENT_ASSERT(e.list_at(1)->string_value() == std::string("aaa"));
		TORRENT_ASSERT(e.list_at(1)->string_length() == 3);
		section = e.list_at(1)->data_section();
		TORRENT_ASSERT(std::memcmp("3:aaa", section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == 5);
	}

	{
		char b[] = "d1:ai12453e1:b3:aaa1:c3:bbb1:X10:0123456789e";
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e, ec);
		TORRENT_ASSERT(ret == 0);
#if TORRENT_USE_IOSTREAM
		std::cout << e << std::endl;
#endif
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(std::memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::dict_t);
		TORRENT_ASSERT(e.dict_size() == 4);
		TORRENT_ASSERT(e.dict_find("a")->type() == lazy_entry::int_t);
		TORRENT_ASSERT(e.dict_find("a")->int_value() == 12453);
		TORRENT_ASSERT(e.dict_find("b")->type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.dict_find("b")->string_value() == std::string("aaa"));
		TORRENT_ASSERT(e.dict_find("b")->string_length() == 3);
		TORRENT_ASSERT(e.dict_find("c")->type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.dict_find("c")->string_value() == std::string("bbb"));
		TORRENT_ASSERT(e.dict_find("c")->string_length() == 3);
		TORRENT_ASSERT(e.dict_find_string_value("X") == "0123456789");
	}

	// test invalid encoding
	{
		char buf[] =
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
		lazy_entry e;
		error_code ec;
		int ret = lazy_bdecode(buf, buf + sizeof(buf), e, ec);
		TEST_CHECK(ret == -1);	
	}
	return 0;
}

