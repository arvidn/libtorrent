/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/torrent_list.hpp"
#include "libtorrent/sha1_hash.hpp"

using namespace lt;

namespace {
info_hash_t ih(char const* s) { return info_hash_t(sha1_hash(s)); }
sha1_hash ih1(char const* s) { return sha1_hash(s); }
}

TORRENT_TEST(torrent_list_empty)
{
	aux::torrent_list<int> l;
	TEST_CHECK(l.empty());
	TEST_CHECK(l.begin() == l.end());
	l.insert(ih("abababababababababab"), std::make_shared<int>(1337));
	TEST_CHECK(!l.empty());
	TEST_CHECK(l.begin() != l.end());
}

TORRENT_TEST(torrent_list_size)
{
	aux::torrent_list<int> l;
	TEST_EQUAL(l.size(), 0);
	l.insert(ih("abababababababababab"), std::make_shared<int>(1337));
	TEST_EQUAL(l.size(), 1);
	l.insert(ih("bcababababababababab"), std::make_shared<int>(1338));
	TEST_EQUAL(l.size(), 2);
	l.insert(ih("cdababababababababab"), std::make_shared<int>(1339));
	TEST_EQUAL(l.size(), 3);
}

TORRENT_TEST(torrent_list_duplicates)
{
	aux::torrent_list<int> l;
	TEST_EQUAL(l.size(), 0);
	TEST_CHECK(l.insert(ih("abababababababababab"), std::make_shared<int>(1337)));
	TEST_EQUAL(l.size(), 1);
	TEST_CHECK(!l.insert(ih("abababababababababab"), std::make_shared<int>(1338)));
	TEST_EQUAL(l.size(), 1);
}

TORRENT_TEST(torrent_list_lookup)
{
	aux::torrent_list<int> l;
	l.insert(ih("abababababababababab"), std::make_shared<int>(1337));
	l.insert(ih("cdababababababababab"), std::make_shared<int>(1338));

	TEST_EQUAL(*l.find(ih1("abababababababababab")), 1337);
	TEST_EQUAL(*l.find(ih1("cdababababababababab")), 1338);
	TEST_CHECK(l.find(ih1("deababababababababab")) == nullptr);
}

TORRENT_TEST(torrent_list_order)
{
	aux::torrent_list<int> l;
	l.insert(ih("abababababababababab"), std::make_shared<int>(1));
	l.insert(ih("cdababababababababab"), std::make_shared<int>(2));
	l.insert(ih("deababababababababab"), std::make_shared<int>(3));
	l.insert(ih("efababababababababab"), std::make_shared<int>(0));

	// iteration order is the same as insertion order, not sort order of
	// info-hashes
	std::vector<int> order;
	for (auto i : l)
	{
		order.push_back(*i);
	}

	TEST_CHECK((order == std::vector<int>{1, 2, 3, 0}));

	TEST_EQUAL(*l[0], 1);
	TEST_EQUAL(*l[1], 2);
	TEST_EQUAL(*l[2], 3);
	TEST_EQUAL(*l[3], 0);
}

TORRENT_TEST(torrent_list_erase)
{
	aux::torrent_list<int> l;
	l.insert(ih("abababababababababab"), std::make_shared<int>(1337));
	TEST_CHECK(!l.empty());

	// this doesn't exist, returns false
	TEST_CHECK(!l.erase(ih("bcababababababababab")));
	TEST_CHECK(!l.empty());

	TEST_EQUAL(*l.find(ih1("abababababababababab")), 1337);
	TEST_CHECK(l.erase(ih("abababababababababab")));
	TEST_CHECK(l.find(ih1("abababababababababab")) == nullptr);
	TEST_CHECK(l.empty());
}

TORRENT_TEST(torrent_list_erase2)
{
	aux::torrent_list<int> l;
	l.insert(ih("abababababababababab"), std::make_shared<int>(1337));
	l.insert(ih("bcababababababababab"), std::make_shared<int>(1338));

	TEST_EQUAL(*l.find(ih1("abababababababababab")), 1337);
	TEST_EQUAL(l.size(), 2);
	TEST_CHECK(!l.empty());

	// delete an entry that isn't the last one
	TEST_CHECK(l.erase(ih("abababababababababab")));
	TEST_CHECK(l.find(ih1("abababababababababab")) == nullptr);
	TEST_EQUAL(l.size(), 1);
	TEST_CHECK(!l.empty());
	TEST_EQUAL(*l.find(ih1("bcababababababababab")), 1338);
}

TORRENT_TEST(torrent_list_clear)
{
	aux::torrent_list<int> l;
	l.insert(ih("abababababababababab"), std::make_shared<int>(1));
	l.insert(ih("cdababababababababab"), std::make_shared<int>(2));
	l.insert(ih("deababababababababab"), std::make_shared<int>(3));
	l.insert(ih("efababababababababab"), std::make_shared<int>(0));

	TEST_CHECK(!l.empty());
	l.clear();
	TEST_CHECK(l.empty());
}

#if !defined TORRENT_DISABLE_ENCRYPTION
TORRENT_TEST(torrent_list_obfuscated_lookup)
{
	sha1_hash const ih("abababababababababab");
	aux::torrent_list<int> l;
	l.insert(info_hash_t(ih), std::make_shared<int>(1337));

	TEST_EQUAL(*l.find(ih), 1337);
	static char const req2[4] = {'r', 'e', 'q', '2'};
	hasher h(req2);
	h.update(ih);
	TEST_EQUAL(*l.find_obfuscated(h.final()), 1337);
	// this should not exist as an obfuscated hash
	TEST_CHECK(l.find_obfuscated(ih) == nullptr);
}
#endif

