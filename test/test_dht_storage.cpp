/*

Copyright (c) 2015, Alden Torres
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

#ifndef TORRENT_DISABLE_DHT

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/kademlia/node.hpp" // for verify_message
#include "libtorrent/bencode.hpp"
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/broadcast_socket.hpp" // for supports_ipv6
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/random.hpp"
#include "libtorrent/ed25519.hpp"

#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/item.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/ed25519.hpp"
#include <numeric>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
using namespace libtorrent::dht;
namespace lt = libtorrent;

namespace
{
	dht_settings test_settings() {
		dht_settings sett;
		sett.max_torrents = 2;
		sett.max_dht_items = 2;
		sett.item_lifetime = seconds(120 * 60).count();
		return sett;
	}

	static sha1_hash to_hash(char const *s) {
		sha1_hash ret;
		from_hex(s, 40, (char *) &ret[0]);
		return ret;
	}

	bool g_storage_constructor_invoked = false;

	dht_storage_interface* dht_custom_storage_constructor(sha1_hash const& id
		, dht_settings const& settings)
	{
		g_storage_constructor_invoked = true;
		return dht_default_storage_constructor(id, settings);
	}
}

const sha1_hash n1 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
const sha1_hash n2 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee402");
const sha1_hash n3 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee403");
const sha1_hash n4 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee404");

TORRENT_TEST(announce_peer)
{
	dht_settings sett = test_settings();
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));
	TEST_CHECK(s.get() != NULL);

	entry peers;
	s->get_peers(n1, false, false, peers);

	TEST_CHECK(peers["n"].string().empty())
	TEST_CHECK(peers["values"].list().empty());

	tcp::endpoint p1 = ep("124.31.75.21", 1);
	tcp::endpoint p2 = ep("124.31.75.22", 1);
	tcp::endpoint p3 = ep("124.31.75.23", 1);
	tcp::endpoint p4 = ep("124.31.75.24", 1);

	s->announce_peer(n1, p1, "torrent_name", false);
	s->get_peers(n1, false, false, peers);
	TEST_EQUAL(peers["n"].string(), "torrent_name")
	TEST_EQUAL(peers["values"].list().size(), 1)

	s->announce_peer(n2, p2, "torrent_name1", false);
	s->announce_peer(n2, p3, "torrent_name1", false);
	s->announce_peer(n3, p4, "torrent_name2", false);
	bool r = s->get_peers(n1, false, false, peers);
	TEST_CHECK(!r);
}

TORRENT_TEST(put_immutable_item)
{
	dht_settings sett = test_settings();
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));
	TEST_CHECK(s.get() != NULL);

	entry item;
	bool r = s->get_immutable_item(n4, item);
	TEST_CHECK(!r);

	s->put_immutable_item(n4, "123", 3, address::from_string("124.31.75.21"));
	r = s->get_immutable_item(n4, item);
	TEST_CHECK(r);

	s->put_immutable_item(n1, "123", 3, address::from_string("124.31.75.21"));
	s->put_immutable_item(n2, "123", 3, address::from_string("124.31.75.21"));
	s->put_immutable_item(n3, "123", 3, address::from_string("124.31.75.21"));
	r = s->get_immutable_item(n1, item);
	TEST_CHECK(!r);

	r = s->get_mutable_item(n4, 0, false, item);
	TEST_CHECK(!r);

	char public_key[item_pk_len];
	char signature[item_sig_len];
	s->put_mutable_item(n4, "123", 3, signature, 1, public_key, "salt", 4, address::from_string("124.31.75.21"));
	r = s->get_mutable_item(n4, 0, false, item);
	TEST_CHECK(r);
}

TORRENT_TEST(counters)
{
	dht_settings sett = test_settings();
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));

	TEST_CHECK(s.get() != NULL);

	sha1_hash n1 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
	sha1_hash n2 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee402");
	sha1_hash n3 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee403");
	sha1_hash n4 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee404");

	TEST_EQUAL(s->counters().peers, 0);
	TEST_EQUAL(s->counters().torrents, 0);

	tcp::endpoint p1 = ep("124.31.75.21", 1);
	tcp::endpoint p2 = ep("124.31.75.22", 1);
	tcp::endpoint p3 = ep("124.31.75.23", 1);
	tcp::endpoint p4 = ep("124.31.75.24", 1);

	s->announce_peer(n1, p1, "torrent_name", false);
	TEST_EQUAL(s->counters().peers, 1);
	TEST_EQUAL(s->counters().torrents, 1);

	s->announce_peer(n2, p2, "torrent_name1", false);
	s->announce_peer(n2, p3, "torrent_name1", false);
	s->announce_peer(n3, p4, "torrent_name2", false);
	TEST_EQUAL(s->counters().peers, 3);
	TEST_EQUAL(s->counters().torrents, 2);

	entry item;

	s->put_immutable_item(n4, "123", 3, address::from_string("124.31.75.21"));
	TEST_EQUAL(s->counters().immutable_data, 1);

	s->put_immutable_item(n1, "123", 3, address::from_string("124.31.75.21"));
	s->put_immutable_item(n2, "123", 3, address::from_string("124.31.75.21"));
	s->put_immutable_item(n3, "123", 3, address::from_string("124.31.75.21"));
	TEST_EQUAL(s->counters().immutable_data, 2);

	char public_key[item_pk_len];
	char signature[item_sig_len];
	s->put_mutable_item(n4, "123", 3, signature, 1, public_key, "salt", 4, address::from_string("124.31.75.21"));
	TEST_EQUAL(s->counters().mutable_data, 1);
}

TORRENT_TEST(set_custom)
{
	g_storage_constructor_invoked = false;
	settings_pack p;
	p.set_bool(settings_pack::enable_dht, false);
	lt::session ses(p);

	TEST_EQUAL(g_storage_constructor_invoked, false);
	bool r = ses.is_dht_running();
	TEST_CHECK(!r);

	ses.set_dht_storage(dht_custom_storage_constructor);

	p.set_bool(settings_pack::enable_dht, true);
	ses.apply_settings(p); // async with dispatch
	r = ses.is_dht_running();
	TEST_CHECK(r);
	TEST_EQUAL(g_storage_constructor_invoked, true);
}

TORRENT_TEST(default_set_custom)
{
	g_storage_constructor_invoked = false;
	settings_pack p;
	p.set_bool(settings_pack::enable_dht, true);
	lt::session ses(p);

	bool r = ses.is_dht_running();
	TEST_CHECK(r);

	ses.set_dht_storage(dht_custom_storage_constructor);

	p.set_bool(settings_pack::enable_dht, false);
	ses.apply_settings(p); // async with dispatch
	r = ses.is_dht_running();
	TEST_CHECK(!r);
	TEST_EQUAL(g_storage_constructor_invoked, false);

	ses.set_dht_storage(dht_custom_storage_constructor);

	p.set_bool(settings_pack::enable_dht, true);
	ses.apply_settings(p); // async with dispatch
	r = ses.is_dht_running();
	TEST_CHECK(r);
	TEST_EQUAL(g_storage_constructor_invoked, true);
}

TORRENT_TEST(peer_limit)
{
	dht_settings sett = test_settings();
	sett.max_peers = 42;
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));
	TEST_CHECK(s.get() != NULL);

	for (int i = 0; i < 200; ++i)
	{
		s->announce_peer(n1, tcp::endpoint(rand_v4(), lt::random())
			, "torrent_name", false);
		dht_storage_counters cnt = s->counters();
		TEST_CHECK(cnt.peers <= 42);
	}
	dht_storage_counters cnt = s->counters();
	TEST_EQUAL(cnt.peers, 42);
}

TORRENT_TEST(torrent_limit)
{
	dht_settings sett = test_settings();
	sett.max_torrents = 42;
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));
	TEST_CHECK(s.get() != NULL);

	for (int i = 0; i < 200; ++i)
	{
		s->announce_peer(rand_hash(), tcp::endpoint(rand_v4(), lt::random())
			, "", false);
		dht_storage_counters cnt = s->counters();
		TEST_CHECK(cnt.torrents <= 42);
	}
	dht_storage_counters cnt = s->counters();
	TEST_EQUAL(cnt.torrents, 42);
}

TORRENT_TEST(immutable_item_limit)
{
	dht_settings sett = test_settings();
	sett.max_dht_items = 42;
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));
	TEST_CHECK(s.get() != NULL);

	for (int i = 0; i < 200; ++i)
	{
		s->put_immutable_item(rand_hash(), "123", 3, rand_v4());
		dht_storage_counters cnt = s->counters();
		TEST_CHECK(cnt.immutable_data <= 42);
	}
	dht_storage_counters cnt = s->counters();
	TEST_EQUAL(cnt.immutable_data, 42);
}

TORRENT_TEST(mutable_item_limit)
{
	dht_settings sett = test_settings();
	sett.max_dht_items = 42;
	boost::scoped_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));
	TEST_CHECK(s.get() != NULL);

	char public_key[item_pk_len];
	char signature[item_sig_len];
	for (int i = 0; i < 200; ++i)
	{
		s->put_mutable_item(rand_hash(), "123", 3, signature, 1, public_key, "salt", 4, rand_v4());
		dht_storage_counters cnt = s->counters();
		TEST_CHECK(cnt.mutable_data <= 42);
	}
	dht_storage_counters cnt = s->counters();
	TEST_EQUAL(cnt.mutable_data, 42);
}

#endif

