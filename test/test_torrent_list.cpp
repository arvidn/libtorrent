/*

Copyright (c) 2008-2012, Arvid Norberg
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
#include "libtorrent/aux_/torrent_list.hpp"
#include "libtorrent/sha1_hash.hpp"

using namespace lt;

TORRENT_TEST(torrent_list_empty)
{
	aux::torrent_list<int> l;
	TEST_CHECK(l.empty());
	TEST_CHECK(l.begin() == l.end());
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1337));
	TEST_CHECK(!l.empty());
	TEST_CHECK(l.begin() != l.end());
}

TORRENT_TEST(torrent_list_size)
{
	aux::torrent_list<int> l;
	TEST_EQUAL(l.size(), 0);
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1337));
	TEST_EQUAL(l.size(), 1);
	l.insert(sha1_hash("bcababababababababab"), std::make_shared<int>(1338));
	TEST_EQUAL(l.size(), 2);
	l.insert(sha1_hash("cdababababababababab"), std::make_shared<int>(1339));
	TEST_EQUAL(l.size(), 3);
}

TORRENT_TEST(torrent_list_duplicates)
{
	aux::torrent_list<int> l;
	TEST_EQUAL(l.size(), 0);
	TEST_CHECK(l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1337)));
	TEST_EQUAL(l.size(), 1);
	TEST_CHECK(!l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1338)));
	TEST_EQUAL(l.size(), 1);
}

TORRENT_TEST(torrent_list_lookup)
{
	aux::torrent_list<int> l;
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1337));
	l.insert(sha1_hash("cdababababababababab"), std::make_shared<int>(1338));

	TEST_EQUAL(*l.find(sha1_hash("abababababababababab")), 1337);
	TEST_EQUAL(*l.find(sha1_hash("cdababababababababab")), 1338);
	TEST_CHECK(l.find(sha1_hash("deababababababababab")) == nullptr);
}

TORRENT_TEST(torrent_list_order)
{
	aux::torrent_list<int> l;
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1));
	l.insert(sha1_hash("cdababababababababab"), std::make_shared<int>(2));
	l.insert(sha1_hash("deababababababababab"), std::make_shared<int>(3));
	l.insert(sha1_hash("efababababababababab"), std::make_shared<int>(0));

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
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1337));
	TEST_CHECK(!l.empty());

	// this doesn't exist, returns false
	TEST_CHECK(!l.erase(sha1_hash("bcababababababababab")));
	TEST_CHECK(!l.empty());

	TEST_EQUAL(*l.find(sha1_hash("abababababababababab")), 1337);
	TEST_CHECK(l.erase(sha1_hash("abababababababababab")));
	TEST_CHECK(l.find(sha1_hash("abababababababababab")) == nullptr);
	TEST_CHECK(l.empty());
}

TORRENT_TEST(torrent_list_erase2)
{
	aux::torrent_list<int> l;
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1337));
	l.insert(sha1_hash("bcababababababababab"), std::make_shared<int>(1338));

	TEST_EQUAL(*l.find(sha1_hash("abababababababababab")), 1337);
	TEST_EQUAL(l.size(), 2);
	TEST_CHECK(!l.empty());

	// delete an entry that isn't the last one
	TEST_CHECK(l.erase(sha1_hash("abababababababababab")));
	TEST_CHECK(l.find(sha1_hash("abababababababababab")) == nullptr);
	TEST_EQUAL(l.size(), 1);
	TEST_CHECK(!l.empty());
	TEST_EQUAL(*l.find(sha1_hash("bcababababababababab")), 1338);
}

TORRENT_TEST(torrent_list_clear)
{
	aux::torrent_list<int> l;
	l.insert(sha1_hash("abababababababababab"), std::make_shared<int>(1));
	l.insert(sha1_hash("cdababababababababab"), std::make_shared<int>(2));
	l.insert(sha1_hash("deababababababababab"), std::make_shared<int>(3));
	l.insert(sha1_hash("efababababababababab"), std::make_shared<int>(0));

	TEST_CHECK(!l.empty());
	l.clear();
	TEST_CHECK(l.empty());
}

#if !defined TORRENT_DISABLE_ENCRYPTION
TORRENT_TEST(torrent_list_obfuscated_lookup)
{
	sha1_hash const ih("abababababababababab");
	aux::torrent_list<int> l;
	l.insert(ih, std::make_shared<int>(1337));

	TEST_EQUAL(*l.find(ih), 1337);
	static char const req2[4] = {'r', 'e', 'q', '2'};
	hasher h(req2);
	h.update(ih);
	TEST_EQUAL(*l.find_obfuscated(h.final()), 1337);
	// this should not exist as an obfuscated hash
	TEST_CHECK(l.find_obfuscated(ih) == nullptr);
}
#endif

