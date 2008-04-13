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
		char b[] = "d1:ai12453e1:b3:aaa1:c3:bbbe";
		lazy_entry e;
		int ret = lazy_bdecode(b, b + sizeof(b)-1, e);
		TORRENT_ASSERT(ret == 0);
		std::cout << e << std::endl;
		std::pair<const char*, int> section = e.data_section();
		TORRENT_ASSERT(memcmp(b, section.first, section.second) == 0);
		TORRENT_ASSERT(section.second == sizeof(b) - 1);
		TORRENT_ASSERT(e.type() == lazy_entry::dict_t);
		TORRENT_ASSERT(e.dict_size() == 3);
		TORRENT_ASSERT(e.dict_find("a")->type() == lazy_entry::int_t);
		TORRENT_ASSERT(e.dict_find("a")->int_value() == 12453);
		TORRENT_ASSERT(e.dict_find("b")->type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.dict_find("b")->string_value() == std::string("aaa"));
		TORRENT_ASSERT(e.dict_find("b")->string_length() == 3);
		TORRENT_ASSERT(e.dict_find("c")->type() == lazy_entry::string_t);
		TORRENT_ASSERT(e.dict_find("c")->string_value() == std::string("bbb"));
		TORRENT_ASSERT(e.dict_find("c")->string_length() == 3);
	}
	return 0;
}

