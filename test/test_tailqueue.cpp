/*

Copyright (c) 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/tailqueue.hpp"

using namespace lt;

namespace {

struct test_node : aux::tailqueue_node<test_node>
{
	explicit test_node(char n) : name(n) {}
	char name;
};

void check_chain(aux::tailqueue<test_node>& chain, char const* expected)
{
	aux::tailqueue_iterator<test_node> i = chain.iterate();

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

void free_chain(aux::tailqueue<test_node>& q)
{
	test_node* chain = static_cast<test_node*>(q.get_all());
	while(chain)
	{
		test_node* del = static_cast<test_node*>(chain);
		chain = static_cast<test_node*>(chain->next);
		delete del;
	}
}

void build_chain(aux::tailqueue<test_node>& q, char const* str)
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

} // anonymous namespace

TORRENT_TEST(tailqueue)
{
	aux::tailqueue<test_node> t1;
	aux::tailqueue<test_node> t2;

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
	test_node* n = t1.get_all();
	TEST_EQUAL(t1.empty(), true);
	TEST_EQUAL(t1.size(), 0);

	char const* expected = "abcdef";
	while (n)
	{
		test_node* del = n;
		TEST_EQUAL(n->name, *expected);
		n = n->next;
		++expected;
		delete del;
	}

	free_chain(t1);
	free_chain(t2);
}
