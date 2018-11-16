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

#ifndef TORRENT_DISABLE_DHT

#include "settings.hpp"
#include "setup_transfer.hpp" // for ep()
#include "libtorrent/config.hpp"
#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"

#include "libtorrent/io_service.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/time.hpp"

#include "simulator/simulator.hpp"

#include <functional>
#include <sstream>

using namespace lt;
using namespace lt::dht;
using namespace sim;
using sim::chrono::high_resolution_clock;
using namespace sim::asio;
using sim::simulation;
using sim::default_config;

using namespace std::placeholders;

namespace
{
	dht::dht_settings test_settings() {
		dht::dht_settings sett;
		sett.max_torrents = 2;
		sett.max_dht_items = 2;
		sett.item_lifetime = int(seconds(120 * 60).count());
		return sett;
	}

	std::unique_ptr<dht_storage_interface> create_default_dht_storage(
		dht::dht_settings const& sett)
	{
		std::unique_ptr<dht_storage_interface> s(dht_default_storage_constructor(sett));
		TEST_CHECK(s.get() != nullptr);

		s->update_node_ids({to_hash("0000000000000000000000000000000000000200")});

		return s;
	}
}

void timer_tick(dht_storage_interface* s
	, dht_storage_counters const& c
	, boost::system::error_code const&)
{
	s->tick();

	TEST_EQUAL(s->counters().peers, c.peers);
	TEST_EQUAL(s->counters().torrents, c.torrents);
	TEST_EQUAL(s->counters().immutable_data, c.immutable_data);
	TEST_EQUAL(s->counters().mutable_data, c.mutable_data);
}

void test_expiration(high_resolution_clock::duration const& expiry_time
	, std::unique_ptr<dht_storage_interface>& s
	, dht_storage_counters const& c)
{
	default_config cfg;
	simulation sim(cfg);
	sim::asio::io_service ios(sim, addr("10.0.0.1"));

	sim::asio::high_resolution_timer timer(ios);
	timer.expires_from_now(expiry_time);
	timer.async_wait(std::bind(&timer_tick, s.get(), c, _1));

	boost::system::error_code ec;
	sim.run(ec);
}

TORRENT_TEST(dht_storage_counters)
{
	dht::dht_settings sett = test_settings();
	std::unique_ptr<dht_storage_interface> s(create_default_dht_storage(sett));

	TEST_CHECK(s.get() != nullptr);

	sha1_hash const n1 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
	sha1_hash const n2 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee402");
	sha1_hash const n3 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee403");
	sha1_hash const n4 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee404");

	tcp::endpoint const p1 = ep("124.31.75.21", 1);
	tcp::endpoint const p2 = ep("124.31.75.22", 1);
	tcp::endpoint const p3 = ep("124.31.75.23", 1);
	tcp::endpoint const p4 = ep("124.31.75.24", 1);

	s->announce_peer(n1, p1, "torrent_name", false);
	s->announce_peer(n2, p2, "torrent_name1", false);
	s->announce_peer(n2, p3, "torrent_name1", false);
	s->announce_peer(n3, p4, "torrent_name2", false);

	s->put_immutable_item(n4, {"123", 3}, addr("124.31.75.21"));
	s->put_immutable_item(n1, {"123", 3}, addr("124.31.75.21"));
	s->put_immutable_item(n2, {"123", 3}, addr("124.31.75.21"));
	s->put_immutable_item(n3, {"123", 3}, addr("124.31.75.21"));

	dht::public_key pk;
	dht::signature sig;
	s->put_mutable_item(n4, {"123", 3}, sig, sequence_number(1), pk, {"salt", 4}
		, addr("124.31.75.21"));

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

TORRENT_TEST(dht_storage_infohashes_sample)
{
	dht::dht_settings sett = test_settings();
	sett.max_torrents = 5;
	sett.sample_infohashes_interval = 30;
	sett.max_infohashes_sample_count = 2;
	std::unique_ptr<dht_storage_interface> s(create_default_dht_storage(sett));

	TEST_CHECK(s.get() != nullptr);

	sha1_hash const n1 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
	sha1_hash const n2 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee402");
	sha1_hash const n3 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee403");
	sha1_hash const n4 = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee404");

	tcp::endpoint const p1 = ep("124.31.75.21", 1);
	tcp::endpoint const p2 = ep("124.31.75.22", 1);
	tcp::endpoint const p3 = ep("124.31.75.23", 1);
	tcp::endpoint const p4 = ep("124.31.75.24", 1);

	s->announce_peer(n1, p1, "torrent_name1", false);
	s->announce_peer(n2, p2, "torrent_name2", false);
	s->announce_peer(n3, p3, "torrent_name3", false);
	s->announce_peer(n4, p4, "torrent_name4", false);

	entry item;
	int r = s->get_infohashes_sample(item);
	TEST_EQUAL(r, 2);

	default_config cfg;
	simulation sim(cfg);
	sim::asio::io_service ios(sim, addr("10.0.0.1"));

	sim::asio::high_resolution_timer timer(ios);
	timer.expires_from_now(hours(1)); // expiration of torrents
	timer.async_wait([&s](boost::system::error_code const&)
	{
		// tick here to trigger the torrents expiration
		s->tick();

		entry item;
		int r = s->get_infohashes_sample(item);
		TEST_EQUAL(r, 0);
	});

	boost::system::error_code ec;
	sim.run(ec);
}
#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_DISABLE_DHT
