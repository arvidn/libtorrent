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
	virtual ~mock_torrent() {}
	bool has_picker() const { return false; }
	bool is_i2p() const { return false; }
	int port_filter_access(int port) const { return 0; }
	int ip_filter_access(address const& addr) const { return 0; }
	piece_picker& picker() { return *((piece_picker*)NULL); }
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

	aux::session_interface& session() { return *((aux::session_interface*)NULL); }
	alert_manager& alerts() const { return *((alert_manager*)NULL); }
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
		peerinfo->connection = c.get();
		return true;
	}

	aux::session_settings sett;

private:

	std::vector<boost::shared_ptr<mock_peer_connection> > m_connections;
};

int test_main()
{

	// test multiple peers with the same IP
	// when disallowing it
	{
		mock_torrent t;
		policy p(&t);
		TEST_EQUAL(p.num_connect_candidates(), 0);
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0);

		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0);
		TEST_EQUAL(p.num_peers(), 1);
		TEST_EQUAL(peer1, peer2);
		TEST_EQUAL(p.num_connect_candidates(), 1);
	}

	// test multiple peers with the same IP
	// when allowing it
	{
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
		policy p(&t);
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		TEST_EQUAL(p.num_peers(), 1);

		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 2);
	}

	{
		mock_torrent t;
		t.sett.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
		policy p(&t);
		torrent_peer* peer1 = p.add_peer(ep("10.0.0.2", 3000), 0, 0);
		TEST_EQUAL(p.num_connect_candidates(), 1);
		TEST_EQUAL(p.num_connect_candidates(), 1);

		TEST_EQUAL(p.num_peers(), 1);
		bool ok = p.connect_one_peer(0);
		TEST_EQUAL(ok, true);

		// we only have one peer, we can't
		// connect another one
		ok = p.connect_one_peer(0);
		TEST_EQUAL(ok, false);
	
		torrent_peer* peer2 = p.add_peer(ep("10.0.0.2", 9020), 0, 0);
		TEST_EQUAL(p.num_peers(), 2);
		TEST_CHECK(peer1 != peer2);
		TEST_EQUAL(p.num_connect_candidates(), 2);

		ok = p.connect_one_peer(0);
		TEST_EQUAL(ok, true);
	}

// TODO: test updating a port that causes a collision
// TODO: add tests here

	return 0;
}

