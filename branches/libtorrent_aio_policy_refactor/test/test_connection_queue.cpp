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
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/connection_interface.hpp"
#include <boost/bind.hpp>

using namespace libtorrent;

int concurrent_connections = 0;
int num_queued = 0;

enum test_type_t
{
	half_open_test,
	timeout_test,
	priority_test
};

char const* test_name[] =
{
	"half-open", "timeout", "priority"
};

struct test_connection : libtorrent::connection_interface
{
	test_connection(io_service& ios, connection_queue& cq, int test_type)
		: m_ios(ios), m_cq(cq), m_ticket(-1), m_type(test_type), m_done(false)
	{
		++num_queued;
		m_cq.enqueue(this, milliseconds(100), 0);
	}
	io_service& m_ios;
	connection_queue& m_cq;
	int m_ticket;
	int m_type;
	bool m_done;

	void on_allow_connect(int ticket)
	{
		fprintf(stderr, "%s: [%p] on_allow_connect(%d)\n", test_name[m_type], this, ticket);
		--num_queued;
		if (ticket < 0) return;
		m_ticket = ticket;
		if (m_type != timeout_test)
			m_ios.post(boost::bind(&test_connection::on_connected, this));
		++concurrent_connections;
		TEST_CHECK(concurrent_connections <= 5);
	}

	void on_connect_timeout()
	{
		fprintf(stderr, "%s: [%p] on_connect_timeout\n", test_name[m_type], this);
		TEST_CHECK(m_type == timeout_test);
		TEST_CHECK(concurrent_connections <= 5);
		--concurrent_connections;
		if (m_type == timeout_test) m_done = true;
	}

	void on_connected()
	{
		fprintf(stderr, "%s: [%p] on_connected\n", test_name[m_type], this);
		TEST_CHECK(m_type != timeout_test);
		TEST_CHECK(concurrent_connections <= 5);
		--concurrent_connections;
		m_cq.done(m_ticket);
		if (m_type == half_open_test) m_done = true;
	}

	~test_connection()
	{
		if (!m_done)
		{
			fprintf(stderr, "%s: failed\n", test_name[m_type]);
			TEST_CHECK(m_done);
		}
	}
};

int test_main()
{

	io_service ios;
	connection_queue cq(ios);

	// test half-open limit
	cq.limit(5);

	std::vector<test_connection*> conns;
	for (int i = 0; i < 20; ++i)
		conns.push_back(new test_connection(ios, cq, half_open_test));

	ios.run();

	TEST_CHECK(concurrent_connections == 0);
	TEST_CHECK(num_queued == 0);
	ios.reset();

	for (int i = 0; i < 20; ++i)
		delete conns[i];

	conns.clear();

	for (int i = 0; i < 5; ++i)
		conns.push_back(new test_connection(ios, cq, timeout_test));

	ios.run();

	for (int i = 0; i < 5; ++i)
		delete conns[i];

	return 0;
}
