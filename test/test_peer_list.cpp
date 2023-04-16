/*

Copyright (c) 2014-2019, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
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

#include "libtorrent/peer_list.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"
#include "libtorrent/peer_connection_interface.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/ip_voter.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/socket_io.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"
#include <vector>
#include <memory> // for shared_ptr
#include <cstdarg>

using namespace lt;

namespace {

struct mock_torrent;

struct mock_peer_connection
	: peer_connection_interface
	, std::enable_shared_from_this<mock_peer_connection>
{
	mock_peer_connection(mock_torrent* tor, bool out)
		: m_choked(false)
		, m_outgoing(out)
		, m_tp(nullptr)
		, m_our_id(nullptr)
		, m_disconnect_called(false)
		, m_torrent(*tor)
	{
		aux::random_bytes(m_id);
	}

	mock_peer_connection(mock_torrent* tor, bool out, tcp::endpoint const& remote)
		: mock_peer_connection(tor, out)
	{
		m_remote = remote;
		m_local = ep("127.0.0.1", 8080);
	}

#if TORRENT_USE_I2P
	mock_peer_connection(mock_torrent* tor, bool out, std::string remote)
		: mock_peer_connection(tor, out)
	{
		m_i2p_destination = std::move(remote);
	}
#endif

	virtual ~mock_peer_connection() = default;

#if !defined TORRENT_DISABLE_LOGGING
	bool should_log(peer_log_alert::direction_t) const noexcept override
	{ return true; }

	void peer_log(peer_log_alert::direction_t, char const* /*event*/
		, char const* fmt, ...) const noexcept override
	{
		va_list v;
		va_start(v, fmt);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
		std::vprintf(fmt, v);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
		va_end(v);
	}
#endif

	bool was_disconnected() const { return m_disconnect_called; }
	void set_local_ep(tcp::endpoint const& ep) { m_local = ep; }

	lt::stat m_stat;
	bool m_choked;
	bool m_outgoing;
	torrent_peer* m_tp;
	tcp::endpoint m_remote;
	tcp::endpoint m_local;
#if TORRENT_USE_I2P
	std::string m_i2p_destination;
	std::string m_local_i2p_endpoint;
#endif
	peer_id m_id;
	peer_id m_our_id;
	bool m_disconnect_called;
	mock_torrent& m_torrent;

#if TORRENT_USE_I2P
	std::string const& destination() const override
	{
		return m_i2p_destination;
	}
	std::string const& local_i2p_endpoint() const override
	{
		return m_local_i2p_endpoint;
	}
#endif

	void get_peer_info(peer_info&) const override {}
	tcp::endpoint const& remote() const override { return m_remote; }
	tcp::endpoint local_endpoint() const override { return m_local; }
	void disconnect(error_code const& ec
		, operation_t op, disconnect_severity_t error = peer_connection_interface::normal) override;
	peer_id const& pid() const override { return m_id; }
	peer_id our_pid() const override { return m_our_id; }
	void set_holepunch_mode() override {}
	torrent_peer* peer_info_struct() const override { return m_tp; }
	void set_peer_info(torrent_peer* pi) override { m_tp = pi; }
	bool is_outgoing() const override { return m_outgoing; }
	void add_stat(std::int64_t downloaded, std::int64_t uploaded) override
	{ m_stat.add_stat(downloaded, uploaded); }
	bool fast_reconnect() const override { return true; }
	bool is_choked() const override { return m_choked; }
	bool failed() const override { return false; }
	lt::stat const& statistics() const override { return m_stat; }
};

struct mock_torrent
{
	explicit mock_torrent(torrent_state* st) : m_p(nullptr), m_state(st) {}
	virtual ~mock_torrent() = default;

	bool connect_to_peer(torrent_peer* peerinfo)
	{
		TORRENT_ASSERT(peerinfo->connection == nullptr);
		if (peerinfo->connection) return false;
		auto c = std::make_shared<mock_peer_connection>(this, true, peerinfo->ip());
		c->set_peer_info(peerinfo);

		m_connections.push_back(c);
		m_p->set_connection(peerinfo, c.get());
		return true;
	}

	peer_list* m_p;
	torrent_state* m_state;
	std::vector<std::shared_ptr<mock_peer_connection>> m_connections;
};

void mock_peer_connection::disconnect(error_code const&
	, operation_t, disconnect_severity_t)
{
	m_torrent.m_p->connection_closed(*this, 0, m_torrent.m_state);
	auto const i = std::find(m_torrent.m_connections.begin(), m_torrent.m_connections.end()
		, std::static_pointer_cast<mock_peer_connection>(shared_from_this()));
	if (i != m_torrent.m_connections.end()) m_torrent.m_connections.erase(i);

	m_tp = nullptr;
	m_disconnect_called = true;
}

bool has_peer(peer_list const& p, tcp::endpoint const& ep)
{
	auto const its = p.find_peers(ep.address());
	return its.first != its.second;
}

torrent_state init_state()
{
	torrent_state st;
	st.is_finished = false;
	st.max_peerlist_size = 1000;
	st.allow_multiple_connections_per_ip = false;
	st.port = 9999;
	return st;
}

torrent_peer* add_peer(peer_list& p, torrent_state& st, tcp::endpoint const& ep)
{
	int cc = p.num_connect_candidates();
	torrent_peer* peer = p.add_peer(ep, {}, {}, &st);
	if (peer)
	{
		TEST_EQUAL(p.num_connect_candidates(), cc + 1);
		TEST_EQUAL(peer->port, ep.port());
	}
	st.erased.clear();
	return peer;
}

#if TORRENT_USE_I2P
torrent_peer* add_i2p_peer(peer_list& p, torrent_state& st, std::string const& destination)
{
	int cc = p.num_connect_candidates();
	torrent_peer* peer = p.add_i2p_peer(destination, {}, {}, &st);
	if (peer)
	{
		TEST_EQUAL(p.num_connect_candidates(), cc + 1);
		TEST_EQUAL(peer->dest(), destination);
	}
	st.erased.clear();
	return peer;
}
#endif

void connect_peer(peer_list& p, mock_torrent& t, torrent_state& st)
{
	torrent_peer* tp = p.connect_one_peer(0, &st);
	TEST_CHECK(tp);
	if (!tp) return;
	t.connect_to_peer(tp);
	st.erased.clear();
	TEST_CHECK(tp->connection);
}

static torrent_peer_allocator allocator;

} // anonymous namespace

// test multiple peers with the same IP
// when disallowing it
TORRENT_TEST(multiple_ips_disallowed)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;
	TEST_EQUAL(p.num_connect_candidates(), 0);
	torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), {}, {}, &st);

	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	st.erased.clear();

	torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), {}, {}, &st);
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(peer1, peer2);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	st.erased.clear();
}

// test multiple peers with the same IP
// when allowing it
TORRENT_TEST(multiple_ips_allowed)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = true;
	peer_list p(allocator);
	t.m_p = &p;
	torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), {}, {}, &st);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	TEST_EQUAL(p.num_peers(), 1);
	st.erased.clear();

	torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), {}, {}, &st);
	TEST_EQUAL(p.num_peers(), 2);
	TEST_CHECK(peer1 != peer2);
	TEST_EQUAL(p.num_connect_candidates(), 2);
	st.erased.clear();
}

// test adding two peers with the same IP, but different ports, to
// make sure they can be connected at the same time
// with allow_multiple_connections_per_ip enabled
TORRENT_TEST(multiple_ips_allowed2)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = true;
	peer_list p(allocator);
	t.m_p = &p;
	torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), {}, {}, &st);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	st.erased.clear();

	TEST_EQUAL(p.num_peers(), 1);
	torrent_peer* tp = p.connect_one_peer(0, &st);
	TEST_CHECK(tp);
	t.connect_to_peer(tp);
	st.erased.clear();

	// we only have one peer, we can't
	// connect another one
	tp = p.connect_one_peer(0, &st);
	TEST_CHECK(tp == nullptr);
	st.erased.clear();

	torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), {}, {}, &st);
	TEST_EQUAL(p.num_peers(), 2);
	TEST_CHECK(peer1 != peer2);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	st.erased.clear();

	tp = p.connect_one_peer(0, &st);
	TEST_CHECK(tp);
	t.connect_to_peer(tp);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	st.erased.clear();
}

// test adding two peers with the same IP, but different ports, to
// make sure they can not be connected at the same time
// with allow_multiple_connections_per_ip disabled
TORRENT_TEST(multiple_ips_disallowed2)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;
	torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), {}, {}, &st);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	TEST_EQUAL(peer1->port, 3000);
	st.erased.clear();

	TEST_EQUAL(p.num_peers(), 1);
	torrent_peer* tp = p.connect_one_peer(0, &st);
	TEST_CHECK(tp);
	t.connect_to_peer(tp);
	st.erased.clear();

	// we only have one peer, we can't
	// connect another one
	tp = p.connect_one_peer(0, &st);
	TEST_CHECK(tp == nullptr);
	st.erased.clear();

	torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), {}, {}, &st);
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(peer2->port, 9020);
		TEST_CHECK(peer1 == peer2);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	st.erased.clear();
}

// test incoming connection
// and update_peer_port
TORRENT_TEST(update_peer_port)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;
	TEST_EQUAL(p.num_connect_candidates(), 0);
	auto c = std::make_shared<mock_peer_connection>(&t, true, ep("10.0.0.1", 8080));
	p.new_connection(*c, 0, &st);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	TEST_EQUAL(p.num_peers(), 1);
	st.erased.clear();

	p.update_peer_port(4000, c->peer_info_struct(), peer_info::incoming, &st);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(c->peer_info_struct()->port, 4000);
	st.erased.clear();
}

// test incoming connection
// and update_peer_port, causing collission
TORRENT_TEST(update_peer_port_collide)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = true;
	peer_list p(allocator);
	t.m_p = &p;

	torrent_peer* peer2 = p.add_peer(ep("10.0.0.1", 4000), {}, {}, &st);
	TEST_CHECK(peer2);

	TEST_EQUAL(p.num_connect_candidates(), 1);
	auto c = std::make_shared<mock_peer_connection>(&t, true, ep("10.0.0.1", 8080));
	p.new_connection(*c, 0, &st);
	TEST_EQUAL(p.num_connect_candidates(), 1);
	// at this point we have two peers, because we think they have different
	// ports
	TEST_EQUAL(p.num_peers(), 2);
	st.erased.clear();

		// this peer will end up having the same port as the existing peer in the list
	p.update_peer_port(4000, c->peer_info_struct(), peer_info::incoming, &st);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	// the expected behavior is to replace that one
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(c->peer_info_struct()->port, 4000);
	st.erased.clear();
}

namespace {
std::shared_ptr<mock_peer_connection> shared_from_this(lt::peer_connection_interface* p)
{
	return std::static_pointer_cast<mock_peer_connection>(
		static_cast<mock_peer_connection*>(p)->shared_from_this());
}
} // anonymous namespace

// test ip filter
TORRENT_TEST(ip_filter)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// add peer 1
	torrent_peer* peer1 = add_peer(p, st, ep("10.0.0.2", 3000));
	torrent_peer* peer2 = add_peer(p, st, ep("11.0.0.2", 9020));

	TEST_CHECK(peer1 != peer2);

	connect_peer(p, t, st);
	connect_peer(p, t, st);

	auto con1 = shared_from_this(peer1->connection);
	TEST_EQUAL(con1->was_disconnected(), false);
	auto con2 = shared_from_this(peer2->connection);
	TEST_EQUAL(con2->was_disconnected(), false);

	// now, filter one of the IPs and make sure the peer is removed
	ip_filter filter;
	filter.add_rule(addr4("11.0.0.0"), addr4("255.255.255.255"), 1);
	std::vector<address> banned;
	p.apply_ip_filter(filter, &st, banned);
	// we just erased a peer, because it was filtered by the ip filter
	TEST_EQUAL(st.erased.size(), 1);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(banned.size(), 1);
	TEST_EQUAL(banned[0], addr4("11.0.0.2"));
	TEST_EQUAL(con2->was_disconnected(), true);
	TEST_EQUAL(con1->was_disconnected(), false);
}

// test port filter
TORRENT_TEST(port_filter)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// add peer 1
	torrent_peer* peer1 = add_peer(p, st, ep("10.0.0.2", 3000));
	torrent_peer* peer2 = add_peer(p, st, ep("11.0.0.2", 9020));

	TEST_CHECK(peer1 != peer2);

	connect_peer(p, t, st);
	connect_peer(p, t, st);

	auto con1 = shared_from_this(peer1->connection);
	TEST_EQUAL(con1->was_disconnected(), false);
	auto con2 = shared_from_this(peer2->connection);
	TEST_EQUAL(con2->was_disconnected(), false);

	// now, filter one of the IPs and make sure the peer is removed
	port_filter filter;
	filter.add_rule(9000, 10000, 1);
	std::vector<tcp::endpoint> banned;
	p.apply_port_filter(filter, &st, banned);
	// we just erased a peer, because it was filtered by the ip filter
	TEST_EQUAL(st.erased.size(), 1);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(banned.size(), 1);
	TEST_EQUAL(banned[0], ep("11.0.0.2", 9020));
	TEST_EQUAL(con2->was_disconnected(), true);
	TEST_EQUAL(con1->was_disconnected(), false);
}

// test banning peers
TORRENT_TEST(ban_peers)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	torrent_peer* peer1 = add_peer(p, st, ep("10.0.0.1", 4000));

	TEST_EQUAL(p.num_connect_candidates(), 1);
	auto c = std::make_shared<mock_peer_connection>(&t, true, ep("10.0.0.1", 8080));
	p.new_connection(*c, 0, &st);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	TEST_EQUAL(p.num_peers(), 1);
	st.erased.clear();

	// now, ban the peer
	bool ok = p.ban_peer(c->peer_info_struct());
	TEST_EQUAL(ok, true);
	TEST_EQUAL(peer1->banned, true);
	// we still have it in the list
	TEST_EQUAL(p.num_peers(), 1);
	// it's just not a connect candidate, nor allowed to receive incoming connections
	TEST_EQUAL(p.num_connect_candidates(), 0);

	p.connection_closed(*c, 0, &st);
	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	st.erased.clear();

	c = std::make_shared<mock_peer_connection>(&t, true, ep("10.0.0.1", 8080));
	ok = p.new_connection(*c, 0, &st);
	// since it's banned, we should not allow this incoming connection
	TEST_EQUAL(ok, false);
	TEST_EQUAL(p.num_connect_candidates(), 0);
	st.erased.clear();
}

// test erase_peers when we fill up the peer list
TORRENT_TEST(erase_peers)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.max_peerlist_size = 100;
	st.allow_multiple_connections_per_ip = true;
	peer_list p(allocator);
	t.m_p = &p;

	for (int i = 0; i < 100; ++i)
	{
		TEST_EQUAL(st.erased.size(), 0);
		tcp::endpoint ep = rand_tcp_ep();
		torrent_peer* peer = add_peer(p, st, ep);
		TEST_CHECK(peer);
		if (peer == nullptr || st.erased.size() > 0)
		{
			std::printf("unexpected rejection of peer: %s | %d in list. "
				"added peer %p, erased %d peers\n"
				, print_endpoint(ep).c_str(), p.num_peers(), static_cast<void*>(peer)
				, int(st.erased.size()));
		}
	}
	TEST_EQUAL(p.num_peers(), 100);

	// trigger the eviction of one peer
	torrent_peer* peer = p.add_peer(rand_tcp_ep(), {}, {}, &st);
	// we either removed an existing peer, or rejected this one
	// either is valid behavior when the list is full
	TEST_CHECK(st.erased.size() == 1 || peer == nullptr);
}

// test set_ip_filter
TORRENT_TEST(set_ip_filter)
{
	torrent_state st = init_state();
	std::vector<address> banned;

	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;

	for (int i = 0; i < 100; ++i)
	{
		p.add_peer(tcp::endpoint(
			address_v4(std::uint32_t((10 << 24) + ((i + 10) << 16))), 353), {}, {}, &st);
		TEST_EQUAL(st.erased.size(), 0);
		st.erased.clear();
	}
	TEST_EQUAL(p.num_peers(), 100);
	TEST_EQUAL(p.num_connect_candidates(), 100);

	// trigger the removal of one peer
	ip_filter filter;
	filter.add_rule(addr4("10.13.0.0"), addr4("10.13.255.255"), ip_filter::blocked);
	p.apply_ip_filter(filter, &st, banned);
	TEST_EQUAL(st.erased.size(), 1);
	TEST_EQUAL(st.erased[0]->address(), addr4("10.13.0.0"));
	TEST_EQUAL(p.num_peers(), 99);
	TEST_EQUAL(p.num_connect_candidates(), 99);
}

// test set_port_filter
TORRENT_TEST(set_port_filter)
{
	torrent_state st = init_state();
	std::vector<tcp::endpoint> banned;

	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;

	for (int i = 0; i < 100; ++i)
	{
		p.add_peer(tcp::endpoint(
			address_v4(std::uint32_t((10 << 24) + ((i + 10) << 16))), std::uint16_t(i + 10)), {}, {}, &st);
		TEST_EQUAL(st.erased.size(), 0);
		st.erased.clear();
	}
	TEST_EQUAL(p.num_peers(), 100);
	TEST_EQUAL(p.num_connect_candidates(), 100);

	// trigger the removal of one peer
	port_filter filter;
	filter.add_rule(13, 13, port_filter::blocked);
	p.apply_port_filter(filter, &st, banned);
	TEST_EQUAL(st.erased.size(), 1);
	TEST_EQUAL(st.erased[0]->address(), addr4("10.13.0.0"));
	TEST_EQUAL(st.erased[0]->port, 13);
	TEST_EQUAL(p.num_peers(), 99);
	TEST_EQUAL(p.num_connect_candidates(), 99);
}

// test set_max_failcount
TORRENT_TEST(set_max_failcount)
{
	torrent_state st = init_state();

	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;

	for (int i = 0; i < 100; ++i)
	{
		torrent_peer* peer = p.add_peer(tcp::endpoint(
			address_v4(std::uint32_t((10 << 24) + ((i + 10) << 16))), std::uint16_t(i + 10)), {}, {}, &st);
		TEST_EQUAL(st.erased.size(), 0);
		st.erased.clear();
		// every other peer has a failcount of 1
		if (i % 2) p.inc_failcount(peer);
	}
	TEST_EQUAL(p.num_peers(), 100);
	TEST_EQUAL(p.num_connect_candidates(), 100);

	// set the max failcount to 1 and observe how half the peers no longer
	// are connect candidates
	st.max_failcount = 1;
	p.set_max_failcount(&st);

	TEST_EQUAL(p.num_connect_candidates(), 50);
	TEST_EQUAL(p.num_peers(), 100);
}

// test set_seed
TORRENT_TEST(set_seed)
{
	torrent_state st = init_state();

	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;

	for (int i = 0; i < 100; ++i)
	{
		torrent_peer* peer = p.add_peer(tcp::endpoint(
			address_v4(std::uint32_t((10 << 24) + ((i + 10) << 16))), std::uint16_t(i + 10)), {}, {}, &st);
		TEST_EQUAL(st.erased.size(), 0);
		st.erased.clear();
		// make every other peer a seed
		if (i % 2) p.set_seed(peer, true);
	}
	TEST_EQUAL(p.num_peers(), 100);
	TEST_EQUAL(p.num_connect_candidates(), 100);

	// now, the torrent completes and we're no longer interested in
	// connecting to seeds. Make sure half the peers are no longer
	// considered connect candidates
	st.is_finished = true;

	// this will make the peer_list recalculate the connect candidates
	std::vector<torrent_peer*> peers;
	p.connect_one_peer(1, &st);

	TEST_EQUAL(p.num_connect_candidates(), 50);
	TEST_EQUAL(p.num_peers(), 100);
}

// test has_peer
TORRENT_TEST(has_peer)
{
	torrent_state st = init_state();
	std::vector<address> banned;

	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;

	torrent_peer* peer1 = add_peer(p, st, ep("10.10.0.1", 10));
	torrent_peer* peer2 = add_peer(p, st, ep("10.10.0.2", 11));

	TEST_EQUAL(p.num_peers(), 2);
	TEST_EQUAL(p.num_connect_candidates(), 2);

	TEST_EQUAL(p.has_peer(peer1), true);
	TEST_EQUAL(p.has_peer(peer2), true);

	ip_filter filter;
	filter.add_rule(addr4("10.10.0.1"), addr4("10.10.0.1"), ip_filter::blocked);
	p.apply_ip_filter(filter, &st, banned);
	TEST_EQUAL(st.erased.size(), 1);
	st.erased.clear();

	TEST_EQUAL(p.num_peers(), 1);
	TEST_EQUAL(p.num_connect_candidates(), 1);

	TEST_EQUAL(p.has_peer(peer1), false);
	TEST_EQUAL(p.has_peer(peer2), true);
}

// test connect_candidates torrent_finish
TORRENT_TEST(connect_candidates_finish)
{
	torrent_state st = init_state();
	std::vector<address> banned;

	mock_torrent t(&st);
	peer_list p(allocator);
	t.m_p = &p;

	torrent_peer* peer1 = add_peer(p, st, ep("10.10.0.1", 10));
	TEST_CHECK(peer1);
	p.set_seed(peer1, true);
	torrent_peer* peer2 = add_peer(p, st, ep("10.10.0.2", 11));
	TEST_CHECK(peer2);
	p.set_seed(peer2, true);
	torrent_peer* peer3 = add_peer(p, st, ep("10.10.0.3", 11));
	TEST_CHECK(peer3);
	p.set_seed(peer3, true);
	torrent_peer* peer4 = add_peer(p, st, ep("10.10.0.4", 11));
	TEST_CHECK(peer4);
	torrent_peer* peer5 = add_peer(p, st, ep("10.10.0.5", 11));
	TEST_CHECK(peer5);

	TEST_EQUAL(p.num_peers(), 5);
	TEST_EQUAL(p.num_connect_candidates(), 5);

	st.is_finished = true;
	// we're finished downloading now, only the non-seeds are
	// connect candidates

	// connect to one of them
	connect_peer(p, t, st);

	TEST_EQUAL(p.num_peers(), 5);
	// and there should be one left
	TEST_EQUAL(p.num_connect_candidates(), 1);
}

// test self-connection
TORRENT_TEST(self_connection)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// add and connect peer
	torrent_peer* peer = add_peer(p, st, ep("10.0.0.2", 3000));
	connect_peer(p, t, st);

	auto con_out = shared_from_this(peer->connection);
	con_out->set_local_ep(ep("10.0.0.2", 8080));

	auto con_in = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.0.2", 8080));
	con_in->set_local_ep(ep("10.0.0.2", 3000));

	p.new_connection(*con_in, 0, &st);

	// from the peer_list's point of view, this looks like we made one
	// outgoing connection and received an incoming one. Since they share
	// the exact same endpoints (IP ports) but just swapped source and
	// destination, the peer list is supposed to figure out that we connected
	// to ourself and disconnect it
	TEST_EQUAL(con_out->was_disconnected(), true);
	TEST_EQUAL(con_in->was_disconnected(), true);
}

// test double connection (both incoming)
TORRENT_TEST(double_connection)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// we are 10.0.0.1 and the other peer is 10.0.0.2

	// first incoming connection
	auto con1 = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.0.2", 7528));
	con1->set_local_ep(ep("10.0.0.1", 8080));

	p.new_connection(*con1, 0, &st);

	// the seconds incoming connection
	auto con2 = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.0.2", 3561));
	con2->set_local_ep(ep("10.0.0.1", 8080));

	p.new_connection(*con2, 0, &st);

	// the second incoming connection should be closed
	TEST_EQUAL(con1->was_disconnected(), false);
	TEST_EQUAL(con2->was_disconnected(), true);
}

// test double connection (we loose)
TORRENT_TEST(double_connection_loose)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// we are 10.0.0.1 and the other peer is 10.0.0.2

	// our outgoing connection
	torrent_peer* peer = add_peer(p, st, ep("10.0.0.2", 3000));
	connect_peer(p, t, st);

	auto con_out = shared_from_this(peer->connection);
	con_out->set_local_ep(ep("10.0.0.1", 3163));

	// and the incoming connection
	auto con_in = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.0.2", 3561));
	con_in->set_local_ep(ep("10.0.0.1", 8080));

	p.new_connection(*con_in, 0, &st);

	// the rules are documented in peer_list.cpp
	TEST_EQUAL(con_out->was_disconnected(), true);
	TEST_EQUAL(con_in->was_disconnected(), false);
}

// test double connection with identical ports (random)
TORRENT_TEST(double_connection_random)
{
	int in = 0;
	int out = 0;
	for (int i = 0; i < 30; ++i)
	{
		torrent_state st = init_state();
		mock_torrent t(&st);
		st.allow_multiple_connections_per_ip = false;
		peer_list p(allocator);
		t.m_p = &p;

		// we are 10.0.0.1 and the other peer is 10.0.0.2

		// our outgoing connection
		torrent_peer* peer = add_peer(p, st, ep("10.0.0.2", 3000));
		connect_peer(p, t, st);

		auto con_out = static_cast<mock_peer_connection*>(peer->connection)->shared_from_this();
		con_out->set_local_ep(ep("10.0.0.1", 3000));

		// and the incoming connection
		auto con_in = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.0.2", 3000));
		con_in->set_local_ep(ep("10.0.0.1", 3000));

		p.new_connection(*con_in, 0, &st);

		// the rules are documented in peer_list.cpp
		out += con_out->was_disconnected();
		in += con_in->was_disconnected();
	}
	// we should have gone different ways randomly
	TEST_CHECK(out > 0);
	TEST_CHECK(in > 0);
}

// test double connection (we win)
TORRENT_TEST(double_connection_win)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// we are 10.0.0.1 and the other peer is 10.0.0.2

	// our outgoing connection
	torrent_peer* peer = add_peer(p, st, ep("10.0.0.2", 8080));
	connect_peer(p, t, st);

	auto con_out = shared_from_this(peer->connection);
	con_out->set_local_ep(ep("10.0.0.1", 3163));

	//and the incoming connection
	auto con_in = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.0.2", 3561));
	con_in->set_local_ep(ep("10.0.0.1", 3000));

	p.new_connection(*con_in, 0, &st);

	// the rules are documented in peer_list.cpp
	TEST_EQUAL(con_out->was_disconnected(), false);
	TEST_EQUAL(con_in->was_disconnected(), true);
}

#if TORRENT_USE_I2P
// test i2p self-connection
TORRENT_TEST(self_connection_i2p)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// add and connect peer
	torrent_peer* peer = add_i2p_peer(p, st, "foobar");
	connect_peer(p, t, st);

	auto con_out = shared_from_this(peer->connection);
	con_out->m_i2p_destination = "foobar";
	con_out->m_local_i2p_endpoint = "foobar";

	auto con_in = std::make_shared<mock_peer_connection>(&t, false, "foobar");
	con_in->m_local_i2p_endpoint = "foobar";

	p.new_connection(*con_in, 0, &st);

	// from the peer_list's point of view, this looks like we made one
	// outgoing connection and received an incoming one. Since they share
	// the exact same endpoints (IP ports) but just swapped source and
	// destination, the peer list is supposed to figure out that we connected
	// to ourself and disconnect it
	TEST_EQUAL(con_out->was_disconnected(), true);
	TEST_EQUAL(con_in->was_disconnected(), true);
}

// test double i2p connection (both incoming)
TORRENT_TEST(double_connection_i2p)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// we are "foo" and the other peer is "bar"

	// first incoming connection
	auto con1 = std::make_shared<mock_peer_connection>(&t, false, "bar");
	con1->m_local_i2p_endpoint = "foo";

	p.new_connection(*con1, 0, &st);

	// the second incoming connection
	auto con2 = std::make_shared<mock_peer_connection>(&t, false, "bar");
	con2->m_local_i2p_endpoint = "foo";

	p.new_connection(*con2, 0, &st);

	// the second incoming connection should be closed
	TEST_EQUAL(con1->was_disconnected(), false);
	TEST_EQUAL(con2->was_disconnected(), true);
}

// test double connection (we loose)
TORRENT_TEST(double_connection_loose_i2p)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// we are "foo" and the other peer is "bar"

	// our outgoing connection
	torrent_peer* peer = add_i2p_peer(p, st, "bar");
	connect_peer(p, t, st);

	auto con_out = shared_from_this(peer->connection);
	con_out->m_local_i2p_endpoint = "foo";

	// and the incoming connection
	auto con_in = std::make_shared<mock_peer_connection>(&t, false, "bar");
	con_in->m_local_i2p_endpoint = "foo";

	p.new_connection(*con_in, 0, &st);

	// the rules are documented in peer_list.cpp
	TEST_EQUAL(con_out->was_disconnected(), true);
	TEST_EQUAL(con_in->was_disconnected(), false);
}

// test double connection (we win)
TORRENT_TEST(double_connection_win_i2p)
{
	torrent_state st = init_state();
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	// we are "bar" and the other peer is "foo"
	// "bar" < "foo", so we gets to make the outgoing connection

	// our outgoing connection
	torrent_peer* peer = add_i2p_peer(p, st, "foo");
	connect_peer(p, t, st);

	auto con_out = shared_from_this(peer->connection);
	con_out->m_local_i2p_endpoint = "bar";

	//and the incoming connection
	auto con_in = std::make_shared<mock_peer_connection>(&t, false, "foo");
	con_in->m_local_i2p_endpoint = "bar";

	p.new_connection(*con_in, 0, &st);

	// the rules are documented in peer_list.cpp
	TEST_EQUAL(con_out->was_disconnected(), true);
	TEST_EQUAL(con_in->was_disconnected(), false);
}
#endif

// test incoming connection when we are at the list size limit
TORRENT_TEST(incoming_size_limit)
{
	torrent_state st = init_state();
	st.max_peerlist_size = 5;
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	torrent_peer* peer1 = add_peer(p, st, ep("10.0.0.1", 8080));
	TEST_CHECK(peer1);
	TEST_EQUAL(p.num_peers(), 1);
	torrent_peer* peer2 = add_peer(p, st, ep("10.0.0.2", 8080));
	TEST_CHECK(peer2);
	TEST_EQUAL(p.num_peers(), 2);
	torrent_peer* peer3 = add_peer(p, st, ep("10.0.0.3", 8080));
	TEST_CHECK(peer3);
	TEST_EQUAL(p.num_peers(), 3);
	torrent_peer* peer4 = add_peer(p, st, ep("10.0.0.4", 8080));
	TEST_CHECK(peer4);
	TEST_EQUAL(p.num_peers(), 4);
	torrent_peer* peer5 = add_peer(p, st, ep("10.0.0.5", 8080));
	TEST_CHECK(peer5);
	TEST_EQUAL(p.num_peers(), 5);

	auto con_in = std::make_shared<mock_peer_connection>(&t, false, ep("10.0.1.2", 3561));
	con_in->set_local_ep(ep("10.0.2.1", 3000));

	// since we're already at 5 peers in the peer list, this call should
	// erase one of the existing ones.
	p.new_connection(*con_in, 0, &st);

	TEST_EQUAL(con_in->was_disconnected(), false);
	TEST_EQUAL(p.num_peers(), 5);

	// one of the previous ones should have been removed
	TEST_EQUAL(has_peer(p, ep("10.0.0.1", 8080))
		+ has_peer(p, ep("10.0.0.2", 8080))
		+ has_peer(p, ep("10.0.0.3", 8080))
		+ has_peer(p, ep("10.0.0.4", 8080))
		+ has_peer(p, ep("10.0.0.5", 8080))
		, 4);
}

// test new peer when we are at the list size limit
TORRENT_TEST(new_peer_size_limit)
{
	torrent_state st = init_state();
	st.max_peerlist_size = 5;
	mock_torrent t(&st);
	st.allow_multiple_connections_per_ip = false;
	peer_list p(allocator);
	t.m_p = &p;

	torrent_peer* peer1 = add_peer(p, st, ep("10.0.0.1", 8080));
	TEST_CHECK(peer1);
	TEST_EQUAL(p.num_peers(), 1);
	torrent_peer* peer2 = add_peer(p, st, ep("10.0.0.2", 8080));
	TEST_CHECK(peer2);
	TEST_EQUAL(p.num_peers(), 2);
	torrent_peer* peer3 = add_peer(p, st, ep("10.0.0.3", 8080));
	TEST_CHECK(peer3);
	TEST_EQUAL(p.num_peers(), 3);
	torrent_peer* peer4 = add_peer(p, st, ep("10.0.0.4", 8080));
	TEST_CHECK(peer4);
	TEST_EQUAL(p.num_peers(), 4);
	torrent_peer* peer5 = add_peer(p, st, ep("10.0.0.5", 8080));
	TEST_CHECK(peer5);
	TEST_EQUAL(p.num_peers(), 5);
	torrent_peer* peer6 = p.add_peer(ep("10.0.0.6", 8080), {}, {}, &st);
	TEST_CHECK(peer6 == nullptr);
	TEST_EQUAL(p.num_peers(), 5);

	// one of the connection should have been removed
	TEST_EQUAL(has_peer(p, ep("10.0.0.1", 8080))
		+ has_peer(p, ep("10.0.0.2", 8080))
		+ has_peer(p, ep("10.0.0.3", 8080))
		+ has_peer(p, ep("10.0.0.4", 8080))
		+ has_peer(p, ep("10.0.0.5", 8080))
		+ has_peer(p, ep("10.0.0.6", 8080))
		, 5);
}

TORRENT_TEST(peer_info_comparison)
{
	peer_address_compare cmp;
	ipv4_peer const ip_low(ep("1.1.1.1", 6888), true, {});
	ipv4_peer const ip_high(ep("100.1.1.1", 6888), true, {});
	TEST_CHECK(cmp(&ip_low, &ip_high));
	TEST_CHECK(!cmp(&ip_high, &ip_low));
	TEST_CHECK(!cmp(&ip_high, &ip_high));
	TEST_CHECK(!cmp(&ip_low, &ip_low));

	TEST_CHECK(!cmp(&ip_low, addr("1.1.1.1")));
	TEST_CHECK(cmp(&ip_low, addr("1.1.1.2")));
	TEST_CHECK(cmp(&ip_low, addr("100.1.1.1")));

	TEST_CHECK(!cmp(addr("1.1.1.1"), &ip_low));
	TEST_CHECK(cmp(addr("1.1.1.0"), &ip_low));
	TEST_CHECK(!cmp( addr("100.1.1.1"), &ip_low));

#if TORRENT_USE_I2P
	i2p_peer const i2p_low("aaaaaa", true, {});
	i2p_peer const i2p_high("zzzzzz", true, {});

	// noremal IPs always sort before i2p addresses
	TEST_CHECK(cmp(&ip_low, &i2p_high));
	TEST_CHECK(cmp(&ip_low, &i2p_low));
	TEST_CHECK(cmp(&ip_high, &i2p_high));
	TEST_CHECK(cmp(&ip_high, &i2p_low));

	// i2p addresses always sort after noremal IPs
	TEST_CHECK(!cmp(&i2p_high, &ip_low));
	TEST_CHECK(!cmp(&i2p_low, &ip_low));
	TEST_CHECK(!cmp(&i2p_high, &ip_high));
	TEST_CHECK(!cmp(&i2p_low, &ip_high));

	// noremal IPs always sort before i2p addresses
	TEST_CHECK(cmp(addr4("1.1.1.1"), &i2p_high));
	TEST_CHECK(cmp(addr4("1.1.1.1"), &i2p_low));
	TEST_CHECK(cmp(addr4("100.1.1.1"), &i2p_high));
	TEST_CHECK(cmp(addr4("100.1.1.1"), &i2p_low));

	// i2p addresses always sort after noremal IPs
	TEST_CHECK(!cmp(&i2p_high, addr4("1.1.1.1")));
	TEST_CHECK(!cmp(&i2p_low, addr4("1.1.1.1")));
	TEST_CHECK(!cmp(&i2p_high, addr4("100.1.1.1")));
	TEST_CHECK(!cmp(&i2p_low, addr4("100.1.1.1")));

	// internal i2p sorting
	TEST_CHECK(cmp(&i2p_low, &i2p_high));
	TEST_CHECK(!cmp(&i2p_high, &i2p_low));
	TEST_CHECK(!cmp(&i2p_high, &i2p_high));
	TEST_CHECK(!cmp(&i2p_low, &i2p_low));

	TEST_CHECK(cmp(&i2p_low, "zzzzzz"));
	TEST_CHECK(!cmp(&i2p_high, "aaaaaa"));
	TEST_CHECK(!cmp(&i2p_high, "zzzzzz"));
	TEST_CHECK(!cmp(&i2p_low, "aaaaaa"));
	TEST_CHECK(cmp(&i2p_low, "aaaaab"));

	TEST_CHECK(cmp("aaaaaa", &i2p_high));
	TEST_CHECK(!cmp("zzzzzz", &i2p_low));
	TEST_CHECK(!cmp("zzzzzz", &i2p_high));
	TEST_CHECK(cmp("zzzzzy", &i2p_high));
	TEST_CHECK(!cmp("aaaaaa", &i2p_low));
#endif
}

#if TORRENT_USE_I2P
TORRENT_TEST(peer_info_set_i2p_destination)
{
	peer_info p;
	TORRENT_ASSERT(!(p.flags & peer_info::i2p_socket));
	p.set_i2p_destination(sha256_hash("................................"));

	TORRENT_ASSERT(p.i2p_destination() == sha256_hash("................................"));
	TORRENT_ASSERT(p.flags & peer_info::i2p_socket);
}
#endif

// TODO: test erasing peers
// TODO: test update_peer_port with allow_multiple_connections_per_ip and without
// TODO: test add i2p peers
// TODO: test allow_i2p_mixed
// TODO: test insert_peer failing with all error conditions
// TODO: test IPv6
// TODO: test connect_to_peer() failing
// TODO: test connection_closed
// TODO: connect candidates recalculation when incrementing failcount
