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
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e);
		TORRENT_ASSERT(ret == 0);
		std::cout << e << std::endl;
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::int_t);
		TORRENT_ASSERT(e.int_value() == 12453);
	}
	
	{
		char b[] = "26:abcdefghijklmnopqrstuvwxyz";
		lazy_entry e;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e);
		TORRENT_ASSERT(ret == 0);
		std::cout << e << std::endl;
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.string_value() == std::string("abcdefghijklmnopqrstuvwxyz"));
		TORRENT_ASSERT(e.string_length() == 26);
	}

	{
		char b[] = "li12453e3:aaae";
		lazy_entry e;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e);
		TORRENT_ASSERT(ret == 0);
		std::cout << e << std::endl;
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::list_t);
		TORRENT_ASSERT(e.list_size() == 2);
		TORRENT_ASSERT(e.list_at(0)->type() == lazy_entry::int_t);
		TORRENT_ASSERT(e.list_at(1)->type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.list_at(0)->int_value() == 12453);
		TORRENT_ASSERT(e.list_at(1)->string_value() == std::string("aaa"));
		TORRENT_ASSERT(e.list_at(1)->string_length() == 3);
		section = e.list_at(1)->data_section();
		TORRENT_ASSERT(memcmp("3:aaa", section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == 5);
	}

	{
		char b[] = "d1:ai12453e1:b3:aaa1:c3:bbb1:X10:0123456789e";
		lazy_entry e;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e);
		TORRENT_ASSERT(ret == 0);
		std::cout << e << std::endl;
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(memcmp(b, section.first, section.second) == 0);
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
	return 0;
}

