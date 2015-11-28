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

#include "test.hpp"
#include "settings.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/kademlia/node.hpp" // for verify_message

#include "libtorrent/kademlia/dht_storage.hpp"

#include "libtorrent/io_service.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/time.hpp"

#include "simulator/simulator.hpp"
#include <functional>

using namespace libtorrent;
using namespace libtorrent::dht;
using namespace sim;
using namespace sim::chrono;
using namespace sim::asio;
using sim::simulation;
using sim::default_config;

namespace
{
	dht_settings test_settings() {
		dht_settings sett;
		sett.max_torrents = 2;
		sett.max_dht_items = 2;
		sett.item_lifetime = seconds(120 * 60).count();
		return sett;
	}

	sha1_hash to_hash(char const *s) {
		sha1_hash ret;
		from_hex(s, 40, (char *) &ret[0]);
		return ret;
	}
}

void timer_tick(boost::shared_ptr<dht_storage_interface> s
	, dht_storage_counters const& c
	, boost::system::error_code const& ec)
{
	libtorrent::aux::update_time_now();
	s->tick();

	TEST_EQUAL(s->counters().peers, c.peers);
	TEST_EQUAL(s->counters().torrents, c.torrents);
	TEST_EQUAL(s->counters().immutable_data, c.immutable_data);
	TEST_EQUAL(s->counters().mutable_data, c.mutable_data);
}

void test_expiration(high_resolution_clock::duration const& expiry_time
	, boost::shared_ptr<dht_storage_interface> s
	, dht_storage_counters const& c)
{
	default_config cfg;
	simulation sim(cfg);
	sim::asio::io_service ios(sim, asio::ip::address_v4::from_string("10.0.0.1"));

	sim::asio::high_resolution_timer timer(ios);
	timer.expires_from_now(expiry_time);
	timer.async_wait(boost::bind(&timer_tick, s, c, _1));

	boost::system::error_code ec;
	sim.run(ec);
}

TORRENT_TEST(dht_storage_counters)
{
	dht_settings sett = test_settings();
	boost::shared_ptr<dht_storage_interface> s(dht_default_storage_constructor(node_id(0), sett));

	TEST_CHECK(s.get() != NULL);

	sha1_hash n1 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
	sha1_hash n2 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee402");
	sha1_hash n3 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee403");
	sha1_hash n4 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee404");

	tcp::endpoint p1 = tcp::endpoint(address::from_string("124.31.75.21"), 1);
	tcp::endpoint p2 = tcp::endpoint(address::from_string("124.31.75.22"), 1);
	tcp::endpoint p3 = tcp::endpoint(address::from_string("124.31.75.23"), 1);
	tcp::endpoint p4 = tcp::endpoint(address::from_string("124.31.75.24"), 1);

	s->announce_peer(n1, p1, "torrent_name", false);
	s->announce_peer(n2, p2, "torrent_name1", false);
	s->announce_peer(n2, p3, "torrent_name1", false);
	s->announce_peer(n3, p4, "torrent_name2", false);

	s->put_immutable_item(n4, "123", 3, address::from_string("124.31.75.21"));

	s->put_immutable_item(n1, "123", 3, address::from_string("124.31.75.21"));
	s->put_immutable_item(n2, "123", 3, address::from_string("124.31.75.21"));
	s->put_immutable_item(n3, "123", 3, address::from_string("124.31.75.21"));

	char public_key[item_pk_len];
	char signature[item_sig_len];
	s->put_mutable_item(n4, "123", 3, signature, 1, public_key, "salt", 4
		, address::from_string("124.31.75.21"));

	dht_storage_counters c;
	// note that we are using the aux global timer

	c.peers = 3;
	c.torrents = 2;
	c.immutable_data = 2;
	c.mutable_data = 1;
	test_expiration(minutes(30), s, c); // test expiration of torrents and peers

	c.peers = 0;
	c.torrents = 0;
	c.immutable_data = 2;
	c.mutable_data = 1;
	test_expiration(minutes(80), s, c); // test expiration of items before 2 hours

	c.peers = 0;
	c.torrents = 0;
	c.immutable_data = 0;
	c.mutable_data = 0;
	test_expiration(hours(1), s, c); // test expiration of everything after 3 hours
}

