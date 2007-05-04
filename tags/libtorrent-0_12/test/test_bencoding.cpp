#include "libtorrent/bencode.hpp"
#include <boost/lexical_cast.hpp>

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
	
	return 0;
}

