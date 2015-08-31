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
#include "libtorrent/linked_list.hpp"

using namespace libtorrent;

struct test_node : list_node<test_node>
{
	test_node(int v) : val(v) {}
	int val;
};

void compare(linked_list<test_node> const& list, int* array, int size)
{
	TEST_EQUAL(list.size(), size);

	int idx = 0;
	for (test_node const* i = list.front(); i != NULL; i = i->next, ++idx)
	{
		TEST_EQUAL(i->val, array[idx]);
	}
}

TORRENT_TEST(push_back)
{
	test_node n0(0);
	test_node n1(1);

	linked_list<test_node> list;

	list.push_back(&n0);
	list.push_back(&n1);

	int expected[] = { 0, 1 };
	compare(list, expected, 2);
}

TORRENT_TEST(push_front)
{
	test_node n0(0);
	test_node n1(1);

	linked_list<test_node> list;

	list.push_back(&n1);
	list.push_front(&n0);

	int expected[] = { 0, 1 };
	compare(list, expected, 2);
}

TORRENT_TEST(erase_begin)
{
	test_node n0(0);
	test_node n1(1);
	test_node n2(2);

	linked_list<test_node> list;

	list.push_back(&n0);
	list.push_back(&n1);
	list.push_back(&n2);

	list.erase(&n0);

	int expected[] = { 1, 2 };
	compare(list, expected, 2);
}

TORRENT_TEST(erase_end)
{
	test_node n0(0);
	test_node n1(1);
	test_node n2(2);

	linked_list<test_node> list;

	list.push_back(&n0);
	list.push_back(&n1);
	list.push_back(&n2);

	list.erase(&n2);

	int expected[] = { 0, 1 };
	compare(list, expected, 2);
}

TORRENT_TEST(erase_middle)
{
	test_node n0(0);
	test_node n1(1);
	test_node n2(2);

	linked_list<test_node> list;

	list.push_back(&n0);
	list.push_back(&n1);
	list.push_back(&n2);

	list.erase(&n1);

	int expected[] = { 0, 2 };
	compare(list, expected, 2);
}

TORRENT_TEST(erase_last)
{
	test_node n0(0);

	linked_list<test_node> list;

	list.push_back(&n0);

	list.erase(&n0);

	int expected[] = { -1 };
	compare(list, expected, 0);

	TEST_CHECK(list.empty());
}

TORRENT_TEST(iterate_forward)
{
	test_node n0(0);
	test_node n1(1);
	test_node n2(2);

	linked_list<test_node> list;

	list.push_back(&n0);
	list.push_back(&n1);
	list.push_back(&n2);

	list_iterator<test_node> it = list.iterate();
	TEST_EQUAL(it.get(), &n0);
	it.next();
	TEST_EQUAL(it.get(), &n1);
	it.next();
	TEST_EQUAL(it.get(), &n2);
	it.next();
	TEST_EQUAL(it.get(), static_cast<test_node*>(NULL));
}

TORRENT_TEST(iterate_backward)
{
	test_node n0(0);
	test_node n1(1);
	test_node n2(2);

	linked_list<test_node> list;

	list.push_back(&n0);
	list.push_back(&n1);
	list.push_back(&n2);

	list_iterator<test_node> it = list.iterate();
	it.next();
	it.next();
	TEST_EQUAL(it.get(), &n2);
	it.prev();
	TEST_EQUAL(it.get(), &n1);
	it.prev();
	TEST_EQUAL(it.get(), &n0);
	it.prev();
	TEST_EQUAL(it.get(), static_cast<test_node*>(NULL));
}

