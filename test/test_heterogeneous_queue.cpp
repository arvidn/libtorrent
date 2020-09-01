/*

Copyright (c) 2015-2017, 2019, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/heterogeneous_queue.hpp"

namespace {

struct A
{
	int a;
	explicit A(int a_) : a(a_) {}
	A(A&&) noexcept = default;
	virtual int type() = 0;
	virtual ~A() = default;
};

struct B : A
{
	int b;
	explicit B(int a_, int b_) : A(a_), b(b_) {}
	B(B&&) noexcept = default;
	int type() override { return 1; }
};

struct C : A
{
	char c[100];
	explicit C(int a_, int c_) : A(a_)
	{
		memset(c, c_, sizeof(c));
	}
	C(C&&) noexcept = default;
	int type() override { return 2; }
};

struct D
{
	static int instances;
	D() { ++instances; }
	D(D const&) { ++instances; }
	D(D&&) noexcept { ++instances; }

	~D() { --instances; }
};

struct E
{
	explicit E(char const* msg) : string_member(msg) {}
	E(E&&) noexcept = default;
	std::string string_member;
};

int D::instances = 0;

struct F
{
	explicit F(int f_)
		: self(this)
		, f(f_)
		, constructed(true)
		, destructed(false)
		, gutted(false)
	{}

	F(F const& f_)
		: self(this), f(f_.f)
		, constructed(f_.constructed)
		, destructed(f_.destructed)
		, gutted(false)
	{
		TEST_EQUAL(f_.constructed, true);
		TEST_EQUAL(f_.destructed, false);
		TEST_EQUAL(f_.gutted, false);
	}

	F(F&& f_) noexcept
		: self(this)
		, f(f_.f)
		, constructed(f_.constructed)
		, destructed(f_.destructed)
		, gutted(f_.gutted)
	{
		TEST_EQUAL(f_.constructed, true);
		TEST_EQUAL(f_.destructed, false);
		TEST_EQUAL(f_.gutted, false);
		f_.gutted = true;
	}

	~F()
	{
		TEST_EQUAL(constructed, true);
		TEST_EQUAL(destructed, false);
		TEST_EQUAL(self, this);
		destructed = true;
		constructed = false;
	}

	// non-copyable
	F& operator=(F const& f) = delete;

	void check_invariant()
	{
		TEST_EQUAL(constructed, true);
		TEST_EQUAL(destructed, false);
		TEST_EQUAL(gutted, false);
		TEST_EQUAL(self, this);
	}

	F* self;
	int f;
	bool constructed;
	bool destructed;
	bool gutted;
};

struct G : A
{
	G(int base, int v) : A(base), g(v) {}
	G(G&&) noexcept = default;
	int type() override { return 3; }
	std::int64_t g;
};

} // anonymous namespace

// test emplace_back of heterogeneous types
// and retrieval of their pointers
TORRENT_TEST(emplace_back)
{
	using namespace lt;

	heterogeneous_queue<A> q;
	q.emplace_back<B>(0, 1);
	TEST_EQUAL(q.size(), 1);
	q.emplace_back<B>(2, 3);
	TEST_EQUAL(q.size(), 2);
	q.emplace_back<B>(4, 5);
	TEST_EQUAL(q.size(), 3);
	q.emplace_back<C>(6, 7);
	TEST_EQUAL(q.size(), 4);
	q.emplace_back<C>(8, 9);
	TEST_EQUAL(q.size(), 5);
	q.emplace_back<C>(10, 11);
	TEST_EQUAL(q.size(), 6);

	std::vector<A*> ptrs;
	q.get_pointers(ptrs);

	TEST_EQUAL(int(ptrs.size()), q.size());
	TEST_EQUAL(ptrs[0]->type(), 1);
	TEST_EQUAL(ptrs[1]->type(), 1);
	TEST_EQUAL(ptrs[2]->type(), 1);
	TEST_EQUAL(ptrs[3]->type(), 2);
	TEST_EQUAL(ptrs[4]->type(), 2);
	TEST_EQUAL(ptrs[5]->type(), 2);

	TEST_EQUAL(static_cast<B*>(ptrs[0])->a, 0);
	TEST_EQUAL(static_cast<B*>(ptrs[0])->b, 1);

	TEST_EQUAL(static_cast<B*>(ptrs[1])->a, 2);
	TEST_EQUAL(static_cast<B*>(ptrs[1])->b, 3);

	TEST_EQUAL(static_cast<B*>(ptrs[2])->a, 4);
	TEST_EQUAL(static_cast<B*>(ptrs[2])->b, 5);

	TEST_EQUAL(static_cast<C*>(ptrs[3])->a, 6);
	TEST_EQUAL(static_cast<C*>(ptrs[3])->c[0], 7);

	TEST_EQUAL(static_cast<C*>(ptrs[4])->a, 8);
	TEST_EQUAL(static_cast<C*>(ptrs[4])->c[0], 9);

	TEST_EQUAL(static_cast<C*>(ptrs[5])->a, 10);
	TEST_EQUAL(static_cast<C*>(ptrs[5])->c[0], 11);
}

TORRENT_TEST(emplace_back_over_aligned)
{
	using namespace lt;

	heterogeneous_queue<A> q;
	q.emplace_back<G>(1, 2);
	q.emplace_back<G>(3, 4);
	q.emplace_back<B>(5, 6);

	std::vector<A*> ptrs;
	q.get_pointers(ptrs);

	TEST_EQUAL(int(ptrs.size()), q.size());
	TEST_EQUAL(ptrs.size(), 3);
	TEST_EQUAL(ptrs[0]->type(), 3);
	TEST_EQUAL(static_cast<G*>(ptrs[0])->a, 1);
	TEST_EQUAL(static_cast<G*>(ptrs[0])->g, 2);
	TEST_EQUAL(ptrs[1]->type(), 3);
	TEST_EQUAL(static_cast<G*>(ptrs[1])->a, 3);
	TEST_EQUAL(static_cast<G*>(ptrs[1])->g, 4);
	TEST_EQUAL(ptrs[2]->type(), 1);
	TEST_EQUAL(static_cast<B*>(ptrs[2])->a, 5);
	TEST_EQUAL(static_cast<B*>(ptrs[2])->b, 6);
}

// test swap
TORRENT_TEST(swap)
{
	using namespace lt;

	heterogeneous_queue<A> q1;
	heterogeneous_queue<A> q2;

	q1.emplace_back<B>(0, 1);
	q1.emplace_back<B>(2, 3);
	q1.emplace_back<B>(4, 5);
	TEST_EQUAL(q1.size(), 3);

	q2.emplace_back<C>(6, 7);
	q2.emplace_back<C>(8, 9);
	TEST_EQUAL(q2.size(), 2);

	std::vector<A*> ptrs;
	q1.get_pointers(ptrs);
	TEST_EQUAL(int(ptrs.size()), q1.size());

	TEST_EQUAL(ptrs[0]->type(), 1);
	TEST_EQUAL(ptrs[1]->type(), 1);
	TEST_EQUAL(ptrs[2]->type(), 1);

	q2.get_pointers(ptrs);
	TEST_EQUAL(int(ptrs.size()), q2.size());

	TEST_EQUAL(ptrs[0]->type(), 2);
	TEST_EQUAL(ptrs[1]->type(), 2);

	q1.swap(q2);

	q1.get_pointers(ptrs);
	TEST_EQUAL(q1.size(), 2);
	TEST_EQUAL(int(ptrs.size()), q1.size());

	TEST_EQUAL(ptrs[0]->type(), 2);
	TEST_EQUAL(ptrs[1]->type(), 2);

	q2.get_pointers(ptrs);
	TEST_EQUAL(q2.size(), 3);
	TEST_EQUAL(int(ptrs.size()), q2.size());

	TEST_EQUAL(ptrs[0]->type(), 1);
	TEST_EQUAL(ptrs[1]->type(), 1);
	TEST_EQUAL(ptrs[2]->type(), 1);
}

// test destruction
TORRENT_TEST(destruction)
{
	using namespace lt;

	heterogeneous_queue<D> q;
	TEST_EQUAL(D::instances, 0);

	q.emplace_back<D>();
	TEST_EQUAL(D::instances, 1);
	q.emplace_back<D>();
	TEST_EQUAL(D::instances, 2);
	q.emplace_back<D>();
	TEST_EQUAL(D::instances, 3);
	q.emplace_back<D>();
	TEST_EQUAL(D::instances, 4);

	q.clear();

	TEST_EQUAL(D::instances, 0);
}

// test copy/move
TORRENT_TEST(copy_move)
{
	using namespace lt;

	heterogeneous_queue<F> q;

	// make sure the queue has to grow at some point, to exercise its
	// copy/move of elements
	for (int i = 0; i < 1000; ++i)
		q.emplace_back<F>(i);

	std::vector<F*> ptrs;
	q.get_pointers(ptrs);

	TEST_EQUAL(int(ptrs.size()), 1000);

	for (std::size_t i = 0; i < ptrs.size(); ++i)
	{
		ptrs[i]->check_invariant();
		TEST_EQUAL(ptrs[i]->f, int(i));
	}

	// destroy all objects, asserting that their invariant still holds
	q.clear();
}

TORRENT_TEST(nontrivial)
{
	using namespace lt;

	heterogeneous_queue<E> q;
	for (int i = 0; i < 10000; ++i)
	{
		q.emplace_back<E>("testing to allocate non-trivial objects");
	}
}
