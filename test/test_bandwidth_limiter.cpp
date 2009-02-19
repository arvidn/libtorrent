/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/bandwidth_queue_entry.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"

#include <boost/lexical_cast.hpp>

struct torrent;
struct peer_connection;

using namespace libtorrent;

const float sample_time = 6.f; // seconds

//#define VERBOSE_LOGGING


struct peer_connection: intrusive_ptr_base<peer_connection>
{
	typedef torrent torrent_type;

	peer_connection(io_service& ios, boost::shared_ptr<torrent> const& t
		, int prio, bool ignore_limits, std::string name);

	bool ignore_bandwidth_limits() { return m_ignore_limits; }
	int max_assignable_bandwidth(int channel) const
	{ return m_bandwidth_limit[channel].max_assignable(); }
	boost::weak_ptr<torrent> associated_torrent() const
	{ return m_torrent; }
	bool is_disconnecting() const { return m_abort; }
	void assign_bandwidth(int channel, int amount);
	void on_transfer(int channel, int amount);
	void start();
	void stop() { m_abort = true; }
	void expire_bandwidth(int channel, int amount);
	void tick();

	int bandwidth_throttle(int channel) const
	{ return m_bandwidth_limit[channel].throttle(); }

	void throttle(int limit) { m_bandwidth_limit[0].throttle(limit); }

	bandwidth_limit m_bandwidth_limit[1];
	boost::weak_ptr<torrent> m_torrent;
	int m_priority;
	bool m_ignore_limits;
	bool m_abort;
	libtorrent::stat m_stats;
	io_service& m_ios;
	std::string m_name;
	bool m_writing;
};

struct torrent
{
	torrent(bandwidth_manager<peer_connection, torrent>& m)
		: m_bandwidth_manager(m)
	{}

	void assign_bandwidth(int channel, int amount, int max_block_size)
	{
#ifdef VERBOSE_LOGGING
		std::cerr << time_now_string()
			<< ": assign bandwidth, " << amount << " blk: " << max_block_size << std::endl;
#endif
		TEST_CHECK(amount > 0);
		TEST_CHECK(amount <= max_block_size);
		if (amount < max_block_size)
			expire_bandwidth(channel, max_block_size - amount);
	}

	int bandwidth_throttle(int channel) const
	{ return m_bandwidth_limit[channel].throttle(); }

	int max_assignable_bandwidth(int channel) const
	{ return m_bandwidth_limit[channel].max_assignable(); }

	void request_bandwidth(int channel
		, boost::intrusive_ptr<peer_connection> const& p
		, int max_block_size
		, int priority)
	{
		TORRENT_ASSERT(max_block_size > 0);
		TORRENT_ASSERT(m_bandwidth_limit[channel].throttle() > 0);
		int block_size = (std::min)(m_bandwidth_limit[channel].throttle() / 10
			, max_block_size);
		if (block_size <= 0) block_size = 1;

		if (m_bandwidth_limit[channel].max_assignable() > 0)
		{
#ifdef VERBOSE_LOGGING
			std::cerr << time_now_string()
				<< ": request bandwidth " << block_size << std::endl;
#endif
			perform_bandwidth_request(channel, p, block_size, priority);
		}
		else
		{
#ifdef VERBOSE_LOGGING
			std::cerr << time_now_string()
				<< ": queue bandwidth request" << block_size << std::endl;
#endif
			// skip forward in the queue until we find a prioritized peer
			// or hit the front of it.
			queue_t::reverse_iterator i = m_bandwidth_queue[channel].rbegin();
			while (i != m_bandwidth_queue[channel].rend() && priority > i->priority)
			{
				++i->priority;
				++i;
			}
			m_bandwidth_queue[channel].insert(i.base(), bw_queue_entry<peer_connection, torrent>(
				p, block_size, priority));
		}
	}

	void expire_bandwidth(int channel, int amount);

	void perform_bandwidth_request(int channel
		, boost::intrusive_ptr<peer_connection> const& p
		, int block_size
		, int priority)
	{
		m_bandwidth_manager.request_bandwidth(p
			, block_size, priority);
		m_bandwidth_limit[channel].assign(block_size);
	}
	bandwidth_limit m_bandwidth_limit[1];
	typedef std::deque<bw_queue_entry<peer_connection, torrent> > queue_t;
	queue_t m_bandwidth_queue[1];
	bandwidth_manager<peer_connection, torrent>& m_bandwidth_manager;
};

peer_connection::peer_connection(io_service& ios, boost::shared_ptr<torrent> const& t
	, int prio, bool ignore_limits, std::string name)
	: m_torrent(t)
	, m_priority(prio)
	, m_ignore_limits(ignore_limits)
	, m_abort(false)
	, m_ios(ios)
	, m_name(name)
	, m_writing(false)
{}

void peer_connection::assign_bandwidth(int channel, int amount)
{
	TEST_CHECK(m_writing);
#ifdef VERBOSE_LOGGING
	std::cerr << time_now_string() << ": [" << m_name
		<< "] assign bandwidth, " << amount << std::endl;
#endif
	TEST_CHECK(amount > 0);
	m_bandwidth_limit[channel].assign(amount);
	m_ios.post(boost::bind(&peer_connection::on_transfer, self(), channel, amount));
}

void peer_connection::on_transfer(int channel, int amount)
{
	TEST_CHECK(m_writing);
	m_writing = false;
	m_stats.sent_bytes(amount, 0);

	boost::shared_ptr<torrent> t = m_torrent.lock();
	if (!t) return;
	if (m_bandwidth_limit[channel].max_assignable() > 0)
	{
		m_writing = true;
		t->request_bandwidth(0, this, 32 * 1024, m_priority);
	}
}

void peer_connection::start()
{
	boost::shared_ptr<torrent> t = m_torrent.lock();
	if (!t) return;
	m_writing = true;
	t->request_bandwidth(0, this, 32 * 1024, m_priority);
}

void peer_connection::expire_bandwidth(int channel, int amount)
{
	TEST_CHECK(amount > 0);
#ifdef VERBOSE_LOGGING
	std::cerr << time_now_string() << ": [" << m_name
		<< "] expire bandwidth, " << amount << std::endl;
#endif
	m_bandwidth_limit[channel].expire(amount);

	if (!m_writing && m_bandwidth_limit[channel].max_assignable() > 0)
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;
		m_writing = true;
		t->request_bandwidth(0, this, 32 * 1024, m_priority);
	}
}

void peer_connection::tick()
{
#ifdef VERBOSE_LOGGING
	std::cerr << time_now_string() << ": [" << m_name
		<< "] tick, rate: " << m_stats.upload_rate() << std::endl;
#endif
	m_stats.second_tick(1.f);
}


void torrent::expire_bandwidth(int channel, int amount)
{
#ifdef VERBOSE_LOGGING
	std::cerr << time_now_string()
		<< ": expire bandwidth, " << amount << std::endl;
#endif
	TEST_CHECK(amount > 0);
	m_bandwidth_limit[channel].expire(amount);
	queue_t tmp;
	while (!m_bandwidth_queue[channel].empty())
	{
		bw_queue_entry<peer_connection, torrent> qe = m_bandwidth_queue[channel].front();
		if (m_bandwidth_limit[channel].max_assignable() == 0)
			break;
		m_bandwidth_queue[channel].pop_front();
		if (qe.peer->max_assignable_bandwidth(channel) <= 0)
		{
			if (!qe.peer->is_disconnecting()) tmp.push_back(qe);
			continue;
		}
		perform_bandwidth_request(channel, qe.peer
			, qe.max_block_size, qe.priority);
	}
	m_bandwidth_queue[channel].insert(m_bandwidth_queue[channel].begin(), tmp.begin(), tmp.end());
}

typedef std::vector<boost::intrusive_ptr<peer_connection> > connections_t;

bool abort_tick = false;

void do_tick(error_code const&e, deadline_timer& tick, connections_t& v)
{
	if (e || abort_tick)
	{
		std::cerr << " tick aborted" << std::endl;
		return;
	}
	std::for_each(v.begin(), v.end()
		, boost::bind(&peer_connection::tick, _1));
	tick.expires_from_now(seconds(1));
	tick.async_wait(boost::bind(&do_tick, _1, boost::ref(tick), boost::ref(v)));
}

void do_stop(deadline_timer& tick, connections_t& v)
{
	abort_tick = true;
	tick.cancel();
	std::for_each(v.begin(), v.end()
		, boost::bind(&peer_connection::stop, _1));
	std::cerr << " stopping..." << std::endl;
}

void do_change_rate(error_code const&e, deadline_timer& tick
	, boost::shared_ptr<torrent> t1
	, boost::shared_ptr<torrent> t2
	, int limit
	, int counter)
{
	TEST_CHECK(!e);
	if (e) return;

	if (counter == 0)
	{
		t1->m_bandwidth_limit[0].throttle(limit);
		t2->m_bandwidth_limit[0].throttle(limit);
		return;
	}

	t1->m_bandwidth_limit[0].throttle(limit + limit / 2 * ((counter & 1)?-1:1));
	t2->m_bandwidth_limit[0].throttle(limit + limit / 2 * ((counter & 1)?1:-1));

	tick.expires_from_now(milliseconds(1600));
	tick.async_wait(boost::bind(&do_change_rate, _1, boost::ref(tick), t1, t2, limit, counter-1));
}

void do_change_peer_rate(error_code const&e, deadline_timer& tick
	, connections_t& v
	, int limit
	, int counter)
{
	TEST_CHECK(!e);
	if (e) return;

	if (counter == 0)
	{
		std::for_each(v.begin(), v.end()
			, boost::bind(&peer_connection::throttle, _1, limit));
		return;
	}

	int c = counter;
	for (connections_t::iterator i = v.begin(); i != v.end(); ++i, ++c)
		i->get()->throttle(limit + limit / 2 * ((c & 1)?-1:1));

	tick.expires_from_now(milliseconds(1100));
	tick.async_wait(boost::bind(&do_change_peer_rate, _1, boost::ref(tick), boost::ref(v), limit, counter-1));
}

void run_test(io_service& ios, connections_t& v)
{
	abort_tick = false;
	std::cerr << "-------------" << std::endl;
	deadline_timer tick(ios);
	tick.expires_from_now(seconds(1));
	tick.async_wait(boost::bind(&do_tick, _1, boost::ref(tick), boost::ref(v)));

	deadline_timer complete(ios);
	complete.expires_from_now(milliseconds(int(sample_time * 1000)));
	complete.async_wait(boost::bind(&do_stop, boost::ref(tick), boost::ref(v)));

	std::for_each(v.begin(), v.end()
		, boost::bind(&peer_connection::start, _1));

	ios.run();
}

bool close_to(float val, float comp, float err)
{
	return fabs(val - comp) <= err;
}

void spawn_connections(connections_t& v, io_service& ios
	, boost::shared_ptr<torrent> t, int num, char const* prefix)
{
	for (int i = 0; i < num; ++i)
	{
		v.push_back(new peer_connection(ios, t, 200, false
			, prefix + boost::lexical_cast<std::string>(i)));
	}
}

void test_equal_connections(int num, int limit)
{
	std::cerr << "\ntest equal connections " << num << " " << limit << std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);
	manager.throttle(limit);

	boost::shared_ptr<torrent> t1(new torrent(manager));

	connections_t v;
	spawn_connections(v, ios, t1, num, "p");
	run_test(ios, v);

	float sum = 0.f;
	float err = (std::max)(limit / num * 0.3f, 1000.f);
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();

		std::cerr << (*i)->m_stats.total_payload_upload() / sample_time
			<< " target: " << (limit / num) << " eps: " << err << std::endl;
		TEST_CHECK(close_to((*i)->m_stats.total_payload_upload() / sample_time, limit / num, err));
	}
	sum /= sample_time;
	std::cerr << "sum: " << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit, 50));
}

void test_connections_variable_rate(int num, int limit, int torrent_limit)
{
	std::cerr << "\ntest connections variable rate" << num
		<< " l: " << limit
		<< " t: " << torrent_limit
		<< std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);

	boost::shared_ptr<torrent> t1(new torrent(manager));
	if (torrent_limit)
		t1->m_bandwidth_limit[0].throttle(torrent_limit);

	connections_t v;
	spawn_connections(v, ios, t1, num, "p");
	std::for_each(v.begin(), v.end()
		, boost::bind(&peer_connection::throttle, _1, limit));

	deadline_timer change_rate(ios);
	change_rate.expires_from_now(milliseconds(1600));
	change_rate.async_wait(boost::bind(&do_change_peer_rate, _1, boost::ref(change_rate)
		, boost::ref(v), limit, 9));
	run_test(ios, v);

	if (torrent_limit > 0 && limit * num > torrent_limit)
		limit = torrent_limit / num;
	
	float sum = 0.f;
	float err = limit * 0.3f;
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();

		std::cerr << (*i)->m_stats.total_payload_upload() / sample_time
			<< " target: " << limit << " eps: " << err << std::endl;
		TEST_CHECK(close_to((*i)->m_stats.total_payload_upload() / sample_time, limit, err));
	}
	sum /= sample_time;
	std::cerr << "sum: " << sum << " target: " << (limit * num) << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit * num, limit * 0.3f * num));
}

void test_single_peer(int limit, bool torrent_limit)
{
	std::cerr << "\ntest single peer " << limit << " " << torrent_limit << std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);
	boost::shared_ptr<torrent> t1(new torrent(manager));

	if (torrent_limit)
		t1->m_bandwidth_limit[0].throttle(limit);
	else
		manager.throttle(limit);

	connections_t v;
	spawn_connections(v, ios, t1, 1, "p");
	run_test(ios, v);

	float sum = 0.f;
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit, 1000));
}

void test_torrents(int num, int limit1, int limit2, int global_limit)
{
	std::cerr << "\ntest equal torrents " << num
		<< " l1: " << limit1
		<< " l2: " << limit2
		<< " g: " << global_limit << std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);
	if (global_limit > 0)
		manager.throttle(global_limit);

	boost::shared_ptr<torrent> t1(new torrent(manager));
	boost::shared_ptr<torrent> t2(new torrent(manager));

	t1->m_bandwidth_limit[0].throttle(limit1);
	t2->m_bandwidth_limit[0].throttle(limit2);

	connections_t v1;
	spawn_connections(v1, ios, t1, num, "t1p");
	connections_t v2;
	spawn_connections(v2, ios, t2, num, "t2p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	std::copy(v2.begin(), v2.end(), std::back_inserter(v));
	run_test(ios, v);

	if (global_limit > 0 && global_limit < limit1 + limit2)
	{
		limit1 = (std::min)(limit1, global_limit / 2);
		limit2 = global_limit - limit1;
	}
	float sum = 0.f;
	for (connections_t::iterator i = v1.begin()
		, end(v1.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit1 << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit1, 1000));

	sum = 0.f;
	for (connections_t::iterator i = v2.begin()
		, end(v2.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit2 << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit2, 1000));
}

void test_torrents_variable_rate(int num, int limit, int global_limit)
{
	std::cerr << "\ntest torrents variable rate" << num
		<< " l: " << limit
		<< " g: " << global_limit << std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);
	if (global_limit > 0)
		manager.throttle(global_limit);

	boost::shared_ptr<torrent> t1(new torrent(manager));
	boost::shared_ptr<torrent> t2(new torrent(manager));

	t1->m_bandwidth_limit[0].throttle(limit);
	t2->m_bandwidth_limit[0].throttle(limit);

	connections_t v1;
	spawn_connections(v1, ios, t1, num, "t1p");
	connections_t v2;
	spawn_connections(v2, ios, t2, num, "t2p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	std::copy(v2.begin(), v2.end(), std::back_inserter(v));

	deadline_timer change_rate(ios);
	change_rate.expires_from_now(milliseconds(1100));
	change_rate.async_wait(boost::bind(&do_change_rate, _1, boost::ref(change_rate), t1, t2, limit, 9));
	
	run_test(ios, v);

	if (global_limit > 0 && global_limit < 2 * limit)
		limit = global_limit / 2;

	float sum = 0.f;
	for (connections_t::iterator i = v1.begin()
		, end(v1.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit, 1000));

	sum = 0.f;
	for (connections_t::iterator i = v2.begin()
		, end(v2.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit, 1000));
}

void test_peer_priority(int limit, bool torrent_limit)
{
	std::cerr << "\ntest peer priority " << limit << " " << torrent_limit << std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);
	boost::shared_ptr<torrent> t1(new torrent(manager));

	if (torrent_limit)
		t1->m_bandwidth_limit[0].throttle(limit);
	else
		manager.throttle(limit);

	connections_t v1;
	spawn_connections(v1, ios, t1, 10, "p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	boost::intrusive_ptr<peer_connection> p(
		new peer_connection(ios, t1, 0, false, "no-priority"));
	v.push_back(p);
	run_test(ios, v);

	float sum = 0.f;
	for (connections_t::iterator i = v1.begin()
		, end(v1.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit, 50));

	std::cerr << "non-prioritized rate: " << p->m_stats.total_payload_upload() / sample_time << std::endl;
	TEST_CHECK(p->m_stats.total_payload_upload() / sample_time < 10);
}

void test_no_starvation(int limit)
{
	std::cerr << "\ntest no starvation " << limit << std::endl;
	io_service ios;
	bandwidth_manager<peer_connection, torrent> manager(ios, 0);
	boost::shared_ptr<torrent> t1(new torrent(manager));
	boost::shared_ptr<torrent> t2(new torrent(manager));

	manager.throttle(limit);

	const int num_peers = 20;

	connections_t v1;
	spawn_connections(v1, ios, t1, num_peers, "p");
	connections_t v;
	std::copy(v1.begin(), v1.end(), std::back_inserter(v));
	boost::intrusive_ptr<peer_connection> p(
		new peer_connection(ios, t2, 0, false, "no-priority"));
	v.push_back(p);
	run_test(ios, v);

	float sum = 0.f;
	for (connections_t::iterator i = v.begin()
		, end(v.end()); i != end; ++i)
	{
		sum += (*i)->m_stats.total_payload_upload();
	}
	sum /= sample_time;
	std::cerr << sum << " target: " << limit << std::endl;
	TEST_CHECK(sum > 0);
	TEST_CHECK(close_to(sum, limit, 50));

	std::cerr << "non-prioritized rate: " << p->m_stats.total_payload_upload() / sample_time << std::endl;
	TEST_CHECK(close_to(p->m_stats.total_payload_upload() / sample_time, limit / (num_peers + 1), 1000));
}

int test_main()
{
	using namespace libtorrent;

	test_equal_connections(2, 20);
	test_equal_connections(2, 2000);
	test_equal_connections(2, 20000);
	test_equal_connections(3, 20000);
	test_equal_connections(5, 20000);
	test_equal_connections(7, 20000);
	test_equal_connections(33, 60000);
	test_equal_connections(33, 500000);
	test_connections_variable_rate(2, 20, 0);
	test_connections_variable_rate(5, 20000, 0);
	test_connections_variable_rate(3, 2000, 6000);
	test_connections_variable_rate(5, 2000, 30000);
	test_connections_variable_rate(33, 500000, 0);
	test_torrents(2, 400, 400, 0);
	test_torrents(2, 100, 500, 0);
	test_torrents(2, 3000, 3000, 6000);
	test_torrents(1, 40000, 40000, 0);
	test_torrents(24, 50000, 50000, 0);
	test_torrents(5, 6000, 6000, 3000);
	test_torrents(5, 6000, 5000, 4000);
	test_torrents(5, 20000, 20000, 30000);
	test_torrents_variable_rate(5, 6000, 3000);
	test_torrents_variable_rate(5, 20000, 30000);
	test_single_peer(40000, true);
	test_single_peer(40000, false);
	test_peer_priority(40000, false);
	test_peer_priority(40000, true);
	test_no_starvation(40000);

	return 0;
}


