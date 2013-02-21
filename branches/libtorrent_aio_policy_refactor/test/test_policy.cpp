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

#include "libtorrent/policy.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/peer_connection_interface.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/ip_voter.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/alert.hpp"

#include "test.hpp"
#include <vector>

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

	libtorrent::stat m_stat;
	bool m_choked;
	bool m_outgoing;
	torrent_peer* m_tp;
	tcp::endpoint m_remote;
	peer_id m_id;

	virtual tcp::endpoint const& remote() const { return m_remote; }
	virtual tcp::endpoint local_endpoint() const { return ep("127.0.0.1", 8080); }
	virtual void disconnect(error_code const& ec, int error = 0) { /* remove from mock_torrent list */}
	virtual peer_id const& pid() const { return m_id; }
	virtual void set_holepunch_mode() {}
	virtual torrent_peer* peer_info_struct() const { return m_tp; }
	virtual void set_peer_info(torrent_peer* pi) { m_tp = pi; }
	virtual bool is_outgoing() const { return m_outgoing; }
	virtual void add_stat(size_type downloaded, size_type uploaded)
	{ m_stat.add_stat(downloaded, uploaded); }
	virtual bool fast_reconnect() const { return true; }
	virtual bool is_choked() const { return m_choked; }
	virtual bool failed() const { return false; }
	virtual libtorrent::stat const& statistics() const { return m_stat; }
};

struct mock_torrent : torrent_interface
{
	mock_torrent() : m_p(NULL), m_am(m_ios, 0, 0) {}
	virtual ~mock_torrent() {}
	bool is_i2p() const { return false; }
	int port_filter_access(int port) const { return 0; }
	int ip_filter_access(address const& addr) const { return m_ip_filter.access(addr); }
	int num_peers() const { return m_connections.size(); }
	aux::session_settings const& settings() const { return sett; }

	torrent_peer* allocate_peer_entry(int type)
	{
		switch(type)
		{
			case aux::session_interface::ipv4_peer: return (torrent_peer*)malloc(sizeof(ipv4_peer));
#if TORRENT_USE_IPV6
			case aux::session_interface::ipv6_peer: return (torrent_peer*)malloc(sizeof(ipv6_peer));
#endif
#if TORRENT_USE_I2P
			case aux::session_interface::i2p_peer: return (torrent_peer*)malloc(sizeof(i2p_peer));
#endif
		}
		return NULL;
	}
	void free_peer_entry(torrent_peer* p) { free(p); }

	external_ip const& external_address() const { return m_ext_ip; }
	int listen_port() const { return 9999; }

	alert_manager& alerts() const { return m_am; }
	bool apply_ip_filter() const { return true; }
	bool is_paused() const { return false; }
	bool is_finished() const { return false; }
	void update_want_peers() {}
	void state_updated() {}
	torrent_handle get_handle() { return torrent_handle(); }
#ifndef TORRENT_DISABLE_EXTENSIONS
	void notify_extension_add_peer(tcp::endpoint const& ip, int src, int flags) {}
#endif
	bool connect_to_peer(torrent_peer* peerinfo, bool ignore_limit = false)
	{
		TORRENT_ASSERT(peerinfo->connection == NULL);
		if (peerinfo->connection) return false;
		boost::shared_ptr<mock_peer_connection> c(new mock_peer_connection(true, peerinfo->ip()));
		m_connections.push_back(c);
		m_p->set_connection(peerinfo, c.get());
		return true;
	}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	std::string name() const { return "mock"; }
	void debug_log(const char* fmt, ...) const
	{
		va_list v;
		va_start(v);
		vprintf(fmt, v);
		va_end(v);
	}
	void session_log(char const* fmt, ...) const
	{
		va_list v;
		va_start(v);
		vprintf(fmt, v);
		va_end(v);
	}
#endif

	ip_filter m_ip_filter;
	external_ip m_ext_ip;
	aux::session_settings sett;
	policy* m_p;
	io_service m_ios;
	mutable alert_manager m_am;

private:

	std::vector<boost::shared_ptr<mock_peer_connection> > m_connections;
};

int test_main()
{

	// test multiple peers with the same IP
	// when disallowing it
	{
		std::vector<torrent_peer*> peers;
		mock_torrent t;
		policy p(&t);
		t.m_p = &p;
		TEST_EQUAL(p.num_connect_candidates(), 0);
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, peers);

		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, peers);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(peer1, peer2);
		TEST_EQUAL(p.num_connect_candidates(), 1);
	}

	// test multiple peers with the same IP
	// when allowing it
	{
		std::vector<torrent_peer*> peers;
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
		policy p(&t);
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, peers);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		TEST_EQUAL(p.num_peers(), 1);

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, peers);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 2);
	}

	// test adding two peers with the same IP, but different ports, to
	// make sure they can be connected at the same time
	// with allow_multiple_connections_per_ip enabled
	{
		std::vector<torrent_peer*> peers;
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
		policy p(&t);
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, peers);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		TEST_EQUAL(p.num_peers(), 1);
		bool ok = p.connect_one_peer(0, peers);
		TEST_EQUAL(ok, true);

		// we only have one peer, we can't
		// connect another one
		ok = p.connect_one_peer(0, peers);
		TEST_EQUAL(ok, false);
	
		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, peers);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		ok = p.connect_one_peer(0, peers);
		TEST_EQUAL(ok, true);
		TEST_EQUAL(p.num_connect_candidates(), 0);
	}

	// test adding two peers with the same IP, but different ports, to
	// make sure they can not be connected at the same time
	// with allow_multiple_connections_per_ip disabled
	{
		std::vector<torrent_peer*> peers;
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, false);
		policy p(&t);
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, peers);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(peer1->port, 3000);

		TEST_EQUAL(p.num_peers(), 1);
		bool ok = p.connect_one_peer(0, peers);
		TEST_EQUAL(ok, true);

		// we only have one peer, we can't
		// connect another one
		ok = p.connect_one_peer(0, peers);
		TEST_EQUAL(ok, false);
	
		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0, peers);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(peer2->port, 9020);
		TEST_CHECK(peer1 == peer2);
		TEST_EQUAL(p.num_connect_candidates(), 0);
	}

	// test incoming connection
	{
		std::vector<torrent_peer*> peers;
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, false);
		policy p(&t);
		t.m_p = &p;
		TEST_EQUAL(p.num_connect_candidates(), 0);
		boost::shared_ptr<mock_peer_connection> c(new mock_peer_connection(true, ep("10.0.0.1", 8080)));
		p.new_connection(*c, 0, peers);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		TEST_EQUAL(p.num_peers(), 1);

		p.update_peer_port(4000, c->peer_info_struct(), peer_info::incoming, peers);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(c->peer_info_struct()->port, 4000);
	}

	// test ip filter
	{
		std::vector<torrent_peer*> peers;
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, false);
		policy p(&t);
		t.m_p = &p;
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0, peers);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(peer1->port, 3000);

		torrent_peer* peer2 = p.add_peer(ep("11.0.0.2", 9020), 0, 0, peers);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_EQUAL(peer2->port, 9020);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 2);

		// now, filter one of the IPs and make sure the peer is removed
		t.m_ip_filter.add_rule(address_v4::from_string("11.0.0.0"), address_v4::from_string("255.255.255.255"), 1);
		p.ip_filter_updated(peers);
		TEST_EQUAL(peers.size(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(p.num_peers(), 1);
	}

// TODO: test updating a port that causes a collision
// TODO: test banning peers
// TODO: test erasing peers
// TODO: test using port and ip filter
// TODO: test incrementing failcount (and make sure we no longer consider the peer a connect canidate)
// TODO: test no_connect_privileged_ports
// TODO: test max peerlist size
// TODO: test logic for which connection to keep when receiving an incoming connection to the same peer as we just made an outgoing connection to
// TODO: test update_peer_port with allow_multiple_connections_per_ip
// TODO: test set_seed
// TODO: test has_peer
// TODO: test insert_peer with a full list
// TODO: test add i2p peers
// TODO: test allow_i2p_mixed
// TODO: test insert_peer failing
// TODO: test IPv6
// TODO: test connect_to_peer() failing
// TODO: test connection_closed
// TODO: test recalculate connect candidates
// TODO: add tests here

	return 0;
}

