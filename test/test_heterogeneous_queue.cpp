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
#include "libtorrent/heterogeneous_queue.hpp"

struct A
{
	int a;
	explicit A(int a_) : a(a_) {}
	virtual int type() = 0;
	virtual ~A() {}
};

struct B : A
{
	int b;
	explicit B(int a_, int b_) : A(a_), b(b_) {}
	virtual int type() { return 1; }
};

struct C : A
{
	char c[100];
	explicit C(int a_, int c_) : A(a_)
	{
		memset(c, c_, sizeof(c));
	}
	virtual int type() { return 2; }
};

struct D
{
	static int instances;
	D() { ++instances; }
	D(D const& d) { ++instances; }

	~D() { --instances; }
};

struct E
{
	E(char const* msg) : string_member(msg) {}
	std::string string_member;
};

int D::instances = 0;

struct F
{
	F(int f_)
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

#if __cplusplus >= 201103L
	F(F&& f_)
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
#endif

	~F()
	{
		TEST_EQUAL(constructed, true);
		TEST_EQUAL(destructed, false);
		TEST_EQUAL(self, this);
		destructed = true;
		constructed = false;
	}

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
private:
	// non-copyable
	F& operator=(F const& f);
};

// test push_back of heterogeneous types
// and retrieval of their pointers
TORRENT_TEST(push_back)
{
	using namespace libtorrent;

	heterogeneous_queue<A> q;
	q.push_back(B(0, 1));
	TEST_EQUAL(q.size(), 1);
	q.push_back(B(2, 3));
	TEST_EQUAL(q.size(), 2);
	q.push_back(B(4, 5));
	TEST_EQUAL(q.size(), 3);
	q.push_back(C(6, 7));
	TEST_EQUAL(q.size(), 4);
	q.push_back(C(8, 9));
	TEST_EQUAL(q.size(), 5);
	q.push_back(C(10, 11));
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

// test swap
TORRENT_TEST(swap)
{
	using namespace libtorrent;

	heterogeneous_queue<A> q1;
	heterogeneous_queue<A> q2;

	q1.push_back(B(0, 1));
	q1.push_back(B(2, 3));
	q1.push_back(B(4, 5));
	TEST_EQUAL(q1.size(), 3);

	q2.push_back(C(6, 7));
	q2.push_back(C(8, 9));
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
	using namespace libtorrent;

	heterogeneous_queue<D> q;
	TEST_EQUAL(D::instances, 0);

	q.push_back(D());
	TEST_EQUAL(D::instances, 1);
	q.push_back(D());
	TEST_EQUAL(D::instances, 2);
	q.push_back(D());
	TEST_EQUAL(D::instances, 3);
	q.push_back(D());
	TEST_EQUAL(D::instances, 4);

	q.clear();

	TEST_EQUAL(D::instances, 0);
}

// test copy/move
TORRENT_TEST(copy_move)
{
	using namespace libtorrent;

	heterogeneous_queue<F> q;

	// make sure the queue has to grow at some point, to exercise its
	// copy/move of elements
	for (int i = 0; i < 1000; ++i)
		q.push_back(F(i));

	std::vector<F*> ptrs;
	q.get_pointers(ptrs);

	TEST_EQUAL(int(ptrs.size()), 1000);

	for (int i = 0; i < int(ptrs.size()); ++i)
	{
		ptrs[i]->check_invariant();
		TEST_EQUAL(ptrs[i]->f, i);
	}

	// destroy all objects, asserting that their invariant still holds
	q.clear();
}

TORRENT_TEST(nontrivial)
{
	using namespace libtorrent;

	heterogeneous_queue<E> q;
	for (int i = 0; i < 10000; ++i)
	{
		q.push_back(E("testing to allocate non-trivial objects"));
	}
}


