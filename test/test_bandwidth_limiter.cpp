/*

Copyright (c) 2007-2009, 2014-2018, 2020, Arvid Norberg
Copyright (c) 2016-2018, 2020, Alden Torres
Copyright (c) 2016-2017, Andrei Kurushin
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

#include "libtorrent/aux_/bandwidth_manager.hpp"
#include "libtorrent/aux_/bandwidth_queue_entry.hpp"
#include "libtorrent/aux_/bandwidth_limit.hpp"
#include "libtorrent/aux_/bandwidth_socket.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/session_settings.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <utility>

struct torrent;
struct peer_connection;

using namespace lt;
using namespace std::placeholders;

namespace {

const float sample_time = 20.f; // seconds

//#define VERBOSE_LOGGING

aux::bandwidth_channel global_bwc;

struct peer_connection: aux::bandwidth_socket, std::enable_shared_from_this<peer_connection>
{
	peer_connection(aux::bandwidth_manager& bwm
		, aux::bandwidth_channel& torrent_bwc, int prio, bool ignore_limits, std::string name)
		: m_bwm(bwm)
		, m_torrent_bandwidth_channel(torrent_bwc)
		, m_priority(prio)
		, m_ignore_limits(ignore_limits)
		, m_name(std::move(name))
		, m_quota(0)
	{}

	bool is_disconnecting() const override { return false; }
	void assign_bandwidth(int channel, int amount) override;

	void throttle(int limit) { m_bandwidth_channel.throttle(limit); }

	void start();

	aux::bandwidth_manager& m_bwm;

	aux::bandwidth_channel m_bandwidth_channel;
	aux::bandwidth_channel& m_torrent_bandwidth_channel;

	int m_priority;
	bool m_ignore_limits;
	std::string m_name;
	std::int64_t m_quota;
};

void peer_connection::assign_bandwidth(int /*channel*/, int amount)
{
	m_quota += amount;
#ifdef VERBOSE_LOGGING
	std::cout << " [" << m_name
		<< "] assign bandwidth, " << amount << std::endl;
#endif
	TEST_CHECK(amount > 0);
	start();
}

void peer_connection::start()
{
	aux::bandwidth_channel* channels[] = {
		&m_bandwidth_channel
		, &m_torrent_bandwidth_channel
		, &global_bwc
	};

	m_bwm.request_bandwidth(shared_from_this(), 400000000, m_priority, channels, 3);
}


using connections_t = std::vector<std::shared_ptr<peer_connection>>;

void do_change_rate(aux::bandwidth_channel& t1, aux::bandwidth_channel& t2, int limit)
{
	static int counter = 10;
	--counter;
	if (counter == 0)
	{
		t1.throttle(limit);
		t2.throttle(limit);
		return;
	}

	t1.throttle(limit + limit / 2 * ((counter & 1)?-1:1));
	t2.throttle(limit + limit / 2 * ((counter & 1)?1:-1));
}

void do_change_peer_rate(connections_t& v, int limit)
{
	static int count = 10;
	--count;
	if (count == 0)
	{
		std::for_each(v.begin(), v.end()
			, std::bind(&peer_connection::throttle, _1, limit));
		return;
	}

	int c = count;
	for (connections_t::iterator i = v.begin(); i != v.end(); ++i, ++c)
		i->get()->throttle(limit + limit / 2 * ((c & 1)?-1:1));
}

static void nop() {}

void run_test(connections_t& v
	, aux::bandwidth_manager& manager
	, std::function<void()> f = &nop)
{
	std::cout << "-------------" << std::endl;

	std::for_each(v.begin(), v.end()
		, std::bind(&peer_connection::start, _1));

	lt::aux::session_settings s;
	int tick_interval = s.get_int(settings_pack::tick_interval);

	for (int i = 0; i < int(sample_time * 1000 / tick_interval); ++i)
	{
		manager.update_quotas(milliseconds(tick_interval));
		if ((i % 15) == 0) f();
	}
}

bool close_to(float const val, float const comp, float const err)
{
	return std::abs(val - comp) <= err;
}

void spawn_connections(connections_t& v, aux::bandwidth_manager& bwm
	, aux::bandwidth_channel& bwc, int num, char const* prefix)
{
	for (int i = 0; i < num; ++i)
	{
		char name[200];
		std::snprintf(name, sizeof(name), "%s%d", prefix, i);
		v.push_back(std::make_shared<peer_connection>(bwm, bwc, 200, false, name));
	}
}

void test_equal_connections(int num, int limit)
{
	std::cout << "\ntest equal connections " << num << " " << limit << std::endl;
	aux::bandwidth_manager manager(0);
	global_bwc.throttle(limit);

	aux::bandwidth_channel t1;

	connections_t v;
	spawn_connections(v, manager, t1, num, "p");
	run_test(v, manager);

	float sum = 0.f;
	float const err = std::max(limit / num * 0.3f, 1000.f);
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;

		std::cout << (*i)->m_quota / sample_time
			<< " target: " << (limit / num) << " eps: " << err << std::endl;
		TEST_CHECK(close_to((*i)->m_quota / sample_time, float(limit) / num, err));
	}
	sum /= sample_time;
	std::cout << "sum: " << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit), 50));
}

void test_connections_variable_rate(int num, int limit, int torrent_limit)
{
	std::cout << "\ntest connections variable rate" << num
		<< " l: " << limit
		<< " t: " << torrent_limit
		<< std::endl;
	aux::bandwidth_manager manager(0);
	global_bwc.throttle(0);

	aux::bandwidth_channel t1;
	if (torrent_limit)
		t1.throttle(torrent_limit);

	connections_t v;
	spawn_connections(v, manager, t1, num, "p");
	std::for_each(v.begin(), v.end()
		, std::bind(&peer_connection::throttle, _1, limit));

	run_test(v, manager, std::bind(&do_change_peer_rate
		, std::ref(v), limit));

	if (torrent_limit > 0 && limit * num > torrent_limit)
		limit = torrent_limit / num;

	float sum = 0.f;
	float err = limit * 0.3f;
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;

		std::cout << (*i)->m_quota / sample_time
			<< " target: " << limit << " eps: " << err << std::endl;
		TEST_CHECK(close_to((*i)->m_quota / sample_time, float(limit), err));
	}
	sum /= sample_time;
	std::cout << "sum: " << sum << " target: " << (limit * num) << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit) * num, limit * 0.3f * num));
}

void test_single_peer(int limit, bool torrent_limit)
{
	std::cout << "\ntest single peer " << limit << " " << torrent_limit << std::endl;
	aux::bandwidth_manager manager(0);
	aux::bandwidth_channel t1;
	global_bwc.throttle(0);

	if (torrent_limit)
		t1.throttle(limit);
	else
		global_bwc.throttle(limit);

	connections_t v;
	spawn_connections(v, manager, t1, 1, "p");
	run_test(v, manager);

	float sum = 0.f;
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit), 1000));
}

void test_torrents(int num, int limit1, int limit2, int global_limit)
{
	std::cout << "\ntest equal torrents " << num
		<< " l1: " << limit1
		<< " l2: " << limit2
		<< " g: " << global_limit << std::endl;
	aux::bandwidth_manager manager(0);
	global_bwc.throttle(global_limit);

	aux::bandwidth_channel t1;
	aux::bandwidth_channel t2;

	t1.throttle(limit1);
	t2.throttle(limit2);

	connections_t v1;
	spawn_connections(v1, manager, t1, num, "t1p");
	connections_t v2;
	spawn_connections(v2, manager, t2, num, "t2p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	std::copy(v2.begin(), v2.end(), std::back_inserter(v));
	run_test(v, manager);

	if (global_limit > 0 && global_limit < limit1 + limit2)
	{
		limit1 = std::min(limit1, global_limit / 2);
		limit2 = global_limit - limit1;
	}
	float sum = 0.f;
	for (connections_t::iterator i = v1.begin()
		, end(v1.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit1 << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit1), 1000));

	sum = 0.f;
	for (connections_t::iterator i = v2.begin()
		, end(v2.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit2 << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit2), 1000));
}

void test_torrents_variable_rate(int num, int limit, int global_limit)
{
	std::cout << "\ntest torrents variable rate" << num
		<< " l: " << limit
		<< " g: " << global_limit << std::endl;
	aux::bandwidth_manager manager(0);
	global_bwc.throttle(global_limit);

	aux::bandwidth_channel t1;
	aux::bandwidth_channel t2;

	t1.throttle(limit);
	t2.throttle(limit);

	connections_t v1;
	spawn_connections(v1, manager, t1, num, "t1p");
	connections_t v2;
	spawn_connections(v2, manager, t2, num, "t2p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	std::copy(v2.begin(), v2.end(), std::back_inserter(v));

	run_test(v, manager, std::bind(&do_change_rate, std::ref(t1), std::ref(t2), limit));

	if (global_limit > 0 && global_limit < 2 * limit)
		limit = global_limit / 2;

	float sum = 0.f;
	for (connections_t::iterator i = v1.begin()
		, end(v1.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit), 1000));

	sum = 0.f;
	for (connections_t::iterator i = v2.begin()
		, end(v2.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit), 1000));
}

void test_peer_priority(int limit, bool torrent_limit)
{
	std::cout << "\ntest peer priority " << limit << " " << torrent_limit << std::endl;
	aux::bandwidth_manager manager(0);
	aux::bandwidth_channel t1;
	global_bwc.throttle(0);

	if (torrent_limit)
		t1.throttle(limit);
	else
		global_bwc.throttle(limit);

	connections_t v1;
	spawn_connections(v1, manager, t1, 10, "p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	std::shared_ptr<peer_connection> p =
		std::make_shared<peer_connection>(manager, t1, 1, false, "no-priority");
	v.push_back(p);
	run_test(v, manager);

	float sum = 0.f;
	for (connections_t::iterator i = v1.begin()
		, end(v1.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit), 50));

	std::cout << "non-prioritized rate: " << p->m_quota / sample_time
		<< " target: " << (limit / 200 / 10) << std::endl;
	TEST_CHECK(close_to(p->m_quota / sample_time, float(limit) / 200 / 10, 5));
}

void test_no_starvation(int limit)
{
	std::cout << "\ntest no starvation " << limit << std::endl;
	aux::bandwidth_manager manager(0);
	aux::bandwidth_channel t1;
	aux::bandwidth_channel t2;

	global_bwc.throttle(limit);

	const int num_peers = 20;

	connections_t v1;
	spawn_connections(v1, manager, t1, num_peers, "p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	std::shared_ptr<peer_connection> p =
		std::make_shared<peer_connection>(manager, t2, 1, false, "no-priority");
	v.push_back(p);
	run_test(v, manager);

	float sum = 0.f;
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_quota;
	}
	sum /= sample_time;
	std::cout << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, float(limit), 50));

	std::cout << "non-prioritized rate: " << p->m_quota / sample_time
		<< " target: " << (limit / 200 / num_peers) << std::endl;
	TEST_CHECK(close_to(p->m_quota / sample_time, float(limit) / 200 / num_peers, 5));
}

} // anonymous namespace

TORRENT_TEST(equal_connection)
{
	test_equal_connections( 2,      20);
	test_equal_connections( 2,    2000);
	test_equal_connections( 2,   20000);
	test_equal_connections( 3,   20000);
	test_equal_connections( 5,   20000);
	test_equal_connections( 7,   20000);
	test_equal_connections(33,   60000);
	test_equal_connections(33,  500000);
	test_equal_connections( 1, 1000000);
	test_equal_connections( 1, 6000000);
}

TORRENT_TEST(conn_var_rate)
{
	test_connections_variable_rate( 2,     20, 0);
	test_connections_variable_rate( 5,  20000, 0);
	test_connections_variable_rate( 3,   2000, 6000);
	test_connections_variable_rate( 5,   2000, 30000);
	test_connections_variable_rate(33, 500000, 0);
}

TORRENT_TEST(torrents)
{
	test_torrents( 2,   400,   400,     0);
	test_torrents( 2,   100,   500,     0);
	test_torrents( 2,  3000,  3000,  6000);
	test_torrents( 1, 40000, 40000,     0);
	test_torrents(24, 50000, 50000,     0);
	test_torrents( 5,  6000,  6000,  3000);
	test_torrents( 5,  6000,  5000,  4000);
	test_torrents( 5, 20000, 20000, 30000);
}

TORRENT_TEST(torrent_var_rate)
{
	test_torrents_variable_rate(5, 6000, 3000);
	test_torrents_variable_rate(5, 20000, 30000);
}

TORRENT_TEST(bandwidth_limiter)
{
	test_single_peer(40000, true);
	test_single_peer(40000, false);
}

TORRENT_TEST(peer_priority)
{
	test_peer_priority(40000, false);
	test_peer_priority(40000, true);
}

TORRENT_TEST(no_starvation)
{
	test_no_starvation(40000);
}
