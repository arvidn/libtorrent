/*

Copyright (c) 2012, Arvid Norberg
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
#include "libtorrent/tailqueue.hpp"

using namespace libtorrent;

struct test_node : tailqueue_node<test_node>
{
	explicit test_node(char n) : name(n) {}
	char name;
};

void check_chain(tailqueue<test_node>& chain, char const* expected)
{
	tailqueue_iterator<test_node> i = chain.iterate();

	while (i.get())
	{
		TEST_EQUAL(static_cast<test_node*>(i.get())->name, *expected);
		i.next();
		++expected;
	}
	if (!chain.empty())
	{
		TEST_CHECK(chain.last() == nullptr || chain.last()->next == nullptr);
	}
	TEST_EQUAL(expected[0], 0);
}

void free_chain(tailqueue<test_node>& q)
{
	test_node* chain = static_cast<test_node*>(q.get_all());
	while(chain)
	{
		test_node* del = static_cast<test_node*>(chain);
		chain = static_cast<test_node*>(chain->next);
		delete del;
	}
}

void build_chain(tailqueue<test_node>& q, char const* str)
{
	free_chain(q);

	char const* expected = str;

	while(*str)
	{
		q.push_back(new test_node(*str));
		++str;
	}
	check_chain(q, expected);
}

TORRENT_TEST(tailqueue)
{
	tailqueue<test_node> t1;
	tailqueue<test_node> t2;

	// test prepend
	build_chain(t1, "abcdef");
	build_chain(t2, "12345");

	t1.prepend(t2);
	check_chain(t1, "12345abcdef");
	check_chain(t2, "");

	// test append
	build_chain(t1, "abcdef");
	build_chain(t2, "12345");

	t1.append(t2);
	check_chain(t1, "abcdef12345");
	check_chain(t2, "");

	// test swap
	build_chain(t1, "abcdef");
	build_chain(t2, "12345");

	t1.swap(t2);

	check_chain(t1, "12345");
	check_chain(t2, "abcdef");

	// test pop_front
	build_chain(t1, "abcdef");

	delete t1.pop_front();

	check_chain(t1, "bcdef");

	// test push_back

	build_chain(t1, "abcdef");
	t1.push_back(new test_node('1'));
	check_chain(t1, "abcdef1");

	// test push_front

	build_chain(t1, "abcdef");
	t1.push_front(new test_node('1'));
	check_chain(t1, "1abcdef");

	// test size

	build_chain(t1, "abcdef");
	TEST_EQUAL(t1.size(), 6);

	// test empty

	free_chain(t1);
	TEST_EQUAL(t1.empty(), true);
	build_chain(t1, "abcdef");
	TEST_EQUAL(t1.empty(), false);

	// test get_all
	build_chain(t1, "abcdef");
	test_node* n = (test_node*)t1.get_all();
	TEST_EQUAL(t1.empty(), true);
	TEST_EQUAL(t1.size(), 0);

	char const* expected = "abcdef";
	while (n)
	{
		test_node* del = n;
		TEST_EQUAL(n->name, *expected);
		n = (test_node*)n->next;
		++expected;
		delete del;
	}

	free_chain(t1);
	free_chain(t2);
}

