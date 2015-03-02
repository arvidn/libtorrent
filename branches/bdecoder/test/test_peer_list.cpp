/*

Copyright (c) 2013, Arvid Norberg
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

#include "test.hpp"
#include "setup_transfer.hpp"
#include <vector>
#include <stdarg.h>

using namespace libtorrent;

tcp::endpoint ep(char const* ip, int port)
{
	return tcp::endpoint(address_v4::from_string(ip), port);
}

struct mock_peer_connection : peer_connection_interface
{
	mock_peer_connection(bool out, tcp::endpoint const& ep)
		: m_choked(false)
		, m_outgoing(out)
		, m_tp(NULL)
		, m_remote(ep)
	{
		for (int i = 0; i < 20; ++i) m_id[i] = rand();
	}
	virtual ~mock_peer_connection() {}
#if defined TORRENT_LOGGING
	virtual void peer_log(char const* fmt, ...) const
	{
		va_list v;	
		va_start(v, fmt);
		vprintf(fmt, v);
		va_end(v);
	}
#endif

	libtorrent::stat m_stat;
	bool m_choked;
	bool m_outgoing;
	torrent_peer* m_tp;
	tcp::endpoint m_remote;
	peer_id m_id;

	virtual void get_peer_info(peer_info& p) const {}
	virtual tcp::endpoint const& remote() const { return m_remote; }
	virtual tcp::endpoint local_endpoint() const { return ep("127.0.0.1", 8080); }
	virtual void disconnect(error_code const& ec
		, operation_t op, int error = 0)
	{ /* remove from mock_torrent list */ m_tp = 0; }
	virtual peer_id const& pid() const { return m_id; }
	virtual void set_holepunch_mode() {}
	virtual torrent_peer* peer_info_struct() const { return m_tp; }
	virtual void set_peer_info(torrent_peer* pi) { m_tp = pi; }
	virtual bool is_outgoing() const { return m_outgoing; }
	virtual void add_stat(boost::int64_t downloaded, boost::int64_t uploaded)
	{ m_stat.add_stat(downloaded, uploaded); }
	virtual bool fast_reconnect() const { return true; }
	virtual bool is_choked() const { return m_choked; }
	virtual bool failed() const { return false; }
	virtual libtorrent::stat const& statistics() const { return m_stat; }
};

struct mock_torrent
{
	mock_torrent() : m_p(NULL) {}
	virtual ~mock_torrent() {}

	bool connect_to_peer(torrent_peer* peerinfo, bool ignore_limit = false)
	{
		TORRENT_ASSERT(peerinfo->connection == NULL);
		if (peerinfo->connection) return false;
		boost::shared_ptr<mock_peer_connection> c(new mock_peer_connection(true, peerinfo->ip()));
		m_connections.push_back(c);
		m_p->set_connection(peerinfo, c.get());
		return true;
	}

#if defined TORRENT_LOGGING
	void debug_log(const char* fmt, ...) const
	{
		va_list v;
		va_start(v, fmt);
		vprintf(fmt, v);
		va_end(v);
	}
#endif

	peer_list* m_p;

private:

	std::vector<boost::shared_ptr<mock_peer_connection> > m_connections;
};

int test_main()
{
	torrent_peer_allocator allocator;
	external_ip ext_ip;

	torrent_state st;
	st.is_finished = false;
	st.is_paused = false;
	st.max_peerlist_size = 1000;
	st.allow_multiple_connections_per_ip = false;
	st.peer_allocator = &allocator;
	st.ip = &ext_ip;
	st.port = 9999;

	// test multiple peers with the same IP
	// when disallowing it
	{
		mock_torrent t;
		peer_list p;
		t.m_p = &p;
		TEST_EQUAL(p.num_connect_candidates(), 0);
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, &st);

		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		st.erased.clear();

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, &st);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(peer1, peer2);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		st.erased.clear();
	}

	// test multiple peers with the same IP
	// when allowing it
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = true;
		peer_list p;
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, &st);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(p.num_peers(), 1);
		st.erased.clear();

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, &st);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 2);
		st.erased.clear();
	}

	// test adding two peers with the same IP, but different ports, to
	// make sure they can be connected at the same time
	// with allow_multiple_connections_per_ip enabled
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = true;
		peer_list p;
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, &st);
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
		TEST_CHECK(tp == NULL);
		st.erased.clear();
	
		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, &st);
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
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = false;
		peer_list p;
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, &st);
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
		TEST_CHECK(tp == NULL);
		st.erased.clear();
	
		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, &st);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(peer2->port, 9020);
		TEST_CHECK(peer1 == peer2);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		st.erased.clear();
	}

	// test incoming connection
	// and update_peer_port
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = false;
		peer_list p;
		t.m_p = &p;
		TEST_EQUAL(p.num_connect_candidates(), 0);
		boost::shared_ptr<mock_peer_connection> c(new mock_peer_connection(true, ep("10.0.0.1", 8080)));
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
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = true;
		peer_list p;
		t.m_p = &p;

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.1", 4000), 0, 0, &st);
		TEST_CHECK(peer2);

		TEST_EQUAL(p.num_connect_candidates(), 1);
		boost::shared_ptr<mock_peer_connection> c(new mock_peer_connection(true, ep("10.0.0.1", 8080)));
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

	// test ip filter
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = false;
		peer_list p;
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, &st);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(peer1->port, 3000);
		st.erased.clear();

		torrent_peer* peer2 = p.add_peer(ep("11.0.0.2", 9020), 0, 0, &st);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_EQUAL(peer2->port, 9020);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 2);
		st.erased.clear();

		// connect both peers
		torrent_peer* tp = p.connect_one_peer(0, &st);
		TEST_CHECK(tp);
		t.connect_to_peer(tp);
		st.erased.clear();

		tp = p.connect_one_peer(0, &st);
		TEST_CHECK(tp);
		t.connect_to_peer(tp);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		st.erased.clear();

		// now, filter one of the IPs and make sure the peer is removed
		ip_filter filter;
		filter.add_rule(address_v4::from_string("11.0.0.0"), address_v4::from_string("255.255.255.255"), 1);
		std::vector<address> banned;
		p.apply_ip_filter(filter, &st, banned);
		// we just erased a peer, because it was filtered by the ip filter
		TEST_EQUAL(st.erased.size(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(banned.size(), 1);
		TEST_EQUAL(banned[0], address_v4::from_string("11.0.0.2"));
	}

	// test banning peers
	{
		mock_torrent t;
		st.allow_multiple_connections_per_ip = false;
		peer_list p;
		t.m_p = &p;

		torrent_peer* peer1 = p.add_peer(ep("10.0.0.1", 4000), 0, 0, &st);
		TEST_CHECK(peer1);
		st.erased.clear();

		TEST_EQUAL(p.num_connect_candidates(), 1);
		boost::shared_ptr<mock_peer_connection> c(new mock_peer_connection(true, ep("10.0.0.1", 8080)));
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

		c.reset(new mock_peer_connection(true, ep("10.0.0.1", 8080)));
		ok = p.new_connection(*c, 0, &st);
		// since it's banned, we should not allow this incoming connection
		TEST_EQUAL(ok, false);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		st.erased.clear();
	}

	// test erase_peers when we fill up the peer list
	{
		mock_torrent t;
		st.max_peerlist_size = 100;
		st.allow_multiple_connections_per_ip = true;
		peer_list p;
		t.m_p = &p;

		for (int i = 0; i < 100; ++i)
		{
			torrent_peer* peer = p.add_peer(rand_tcp_ep(), 0, 0, &st);
			TEST_EQUAL(st.erased.size(), 0);
			st.erased.clear();
			TEST_CHECK(peer);
			if (peer == NULL || st.erased.size() > 0)
			{
				fprintf(stderr, "unexpected rejection of peer: %d in list. added peer %p, erased %d peers\n"
					, p.num_peers(), peer, int(st.erased.size()));
			}
		}
		TEST_EQUAL(p.num_peers(), 100);

		// trigger the eviction of one peer
		torrent_peer* peer = p.add_peer(rand_tcp_ep(), 0, 0, &st);
		// we either removed an existing peer, or rejected this one
		// either is valid behavior when the list is full
		TEST_CHECK(st.erased.size() == 1 || peer == NULL);
	}

	// test set_ip_filter
	{
		std::vector<address> banned;
		st.erased.clear();

		mock_torrent t;
		peer_list p;
		t.m_p = &p;

		for (int i = 0; i < 100; ++i)
		{
			p.add_peer(tcp::endpoint(
				address_v4((10 << 24) + ((i + 10) << 16)), 353), 0, 0, &st);
			TEST_EQUAL(st.erased.size(), 0);
			st.erased.clear();
		}
		TEST_EQUAL(p.num_peers(), 100);
		TEST_EQUAL(p.num_connect_candidates(), 100);

		// trigger the removal of one peer
		ip_filter filter;
		filter.add_rule(address_v4::from_string("10.13.0.0")
			, address_v4::from_string("10.13.255.255"), ip_filter::blocked);
		p.apply_ip_filter(filter, &st, banned);
		TEST_EQUAL(st.erased.size(), 1);
		TEST_EQUAL(st.erased[0]->address(), address_v4::from_string("10.13.0.0"));
		TEST_EQUAL(p.num_peers(), 99);
		TEST_EQUAL(p.num_connect_candidates(), 99);
	}

	// test set_port_filter
	{
		std::vector<address> banned;
		st.erased.clear();

		mock_torrent t;
		peer_list p;
		t.m_p = &p;

		for (int i = 0; i < 100; ++i)
		{
			p.add_peer(tcp::endpoint(
				address_v4((10 << 24) + ((i + 10) << 16)), i + 10), 0, 0, &st);
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
		TEST_EQUAL(st.erased[0]->address(), address_v4::from_string("10.13.0.0"));
		TEST_EQUAL(st.erased[0]->port, 13);
		TEST_EQUAL(p.num_peers(), 99);
		TEST_EQUAL(p.num_connect_candidates(), 99);
	}

	// test set_max_failcount
	{
		st.erased.clear();

		mock_torrent t;
		peer_list p;
		t.m_p = &p;

		for (int i = 0; i < 100; ++i)
		{
			torrent_peer* peer = p.add_peer(tcp::endpoint(
				address_v4((10 << 24) + ((i + 10) << 16)), i + 10), 0, 0, &st);
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
	{
		st.erased.clear();

		mock_torrent t;
		peer_list p;
		t.m_p = &p;

		for (int i = 0; i < 100; ++i)
		{
			torrent_peer* peer = p.add_peer(tcp::endpoint(
				address_v4((10 << 24) + ((i + 10) << 16)), i + 10), 0, 0, &st);
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
		torrent_peer* peer = p.connect_one_peer(1, &st);

		TEST_EQUAL(p.num_connect_candidates(), 50);
		TEST_EQUAL(p.num_peers(), 100);
	}

	// test has_peer
	{
		std::vector<address> banned;
		st.erased.clear();

		mock_torrent t;
		peer_list p;
		t.m_p = &p;

		torrent_peer* peer1 = p.add_peer(tcp::endpoint(
			address_v4::from_string("10.10.0.1"), 10), 0, 0, &st);
		TEST_EQUAL(st.erased.size(), 0);
		st.erased.clear();

		torrent_peer* peer2 = p.add_peer(tcp::endpoint(
			address_v4::from_string("10.10.0.2"), 11), 0, 0, &st);
		TEST_EQUAL(st.erased.size(), 0);
		st.erased.clear();

		TEST_EQUAL(p.num_peers(), 2);
		TEST_EQUAL(p.num_connect_candidates(), 2);

		TEST_EQUAL(p.has_peer(peer1), true);
		TEST_EQUAL(p.has_peer(peer2), true);

		ip_filter filter;
		filter.add_rule(address_v4::from_string("10.10.0.1")
			, address_v4::from_string("10.10.0.1"), ip_filter::blocked);
		p.apply_ip_filter(filter, &st, banned);
		TEST_EQUAL(st.erased.size(), 1);
		st.erased.clear();

		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		TEST_EQUAL(p.has_peer(peer1), false);
		TEST_EQUAL(p.has_peer(peer2), true);
	}

// TODO: test erasing peers
// TODO: test logic for which connection to keep when receiving an incoming
// connection to the same peer as we just made an outgoing connection to
// TODO: test update_peer_port with allow_multiple_connections_per_ip and without
// TODO: test add i2p peers
// TODO: test allow_i2p_mixed
// TODO: test insert_peer failing with all error conditions
// TODO: test IPv6
// TODO: test connect_to_peer() failing
// TODO: test connection_closed

	return 0;
}

