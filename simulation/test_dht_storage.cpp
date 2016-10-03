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

#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for ep()
#include "libtorrent/config.hpp"
#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/kademlia/item.hpp"

#include "libtorrent/io_service.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/time.hpp"

#include "simulator/simulator.hpp"

#include <functional>
#include <sstream>
#include <chrono>

using namespace libtorrent;
using namespace libtorrent::dht;
using namespace sim;
using namespace sim::chrono;
using namespace sim::asio;
using sim::simulation;
using sim::default_config;

using namespace std::placeholders;

namespace
{
	dht_settings test_settings() {
		dht_settings sett;
		sett.max_torrents = 2;
		sett.max_dht_items = 2;
		sett.item_lifetime = int(seconds(120 * 60).count());
		return sett;
	}

	std::unique_ptr<dht_storage_interface> create_default_dht_storage(
		dht_settings const& sett, dht_storage_items items = dht_storage_items())
	{
		std::unique_ptr<dht_storage_interface> s(dht_default_storage_constructor(sett
			, std::move(items)));
		TEST_CHECK(s.get() != nullptr);

		s->update_node_ids({to_hash("0000000000000000000000000000000000000200")});

		return s;
	}
}

void timer_tick(dht_storage_interface* s
	, dht_storage_counters const& c
	, boost::system::error_code const&)
{
	libtorrent::aux::update_time_now();
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

dht_storage_items test_save_expiration(high_resolution_clock::duration const& expiry_time
	, std::unique_ptr<dht_storage_interface>& s)
{
	dht_storage_items ret;

	default_config cfg;
	simulation sim(cfg);
	sim::asio::io_service ios(sim, addr("10.0.0.1"));

	sim::asio::high_resolution_timer timer(ios);
	timer.expires_from_now(expiry_time);
	timer.async_wait([&](boost::system::error_code const&)
	{
		libtorrent::aux::update_time_now();
		s->tick();
		ret = s->export_items();
	});

	boost::system::error_code ec;
	sim.run(ec);

	return ret;
}

TORRENT_TEST(dht_storage_counters)
{
	dht_settings sett = test_settings();
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

TORRENT_TEST(export_load_items)
{
	using std::chrono::system_clock;

	dht_settings sett = test_settings();
	std::unique_ptr<dht_storage_interface> s(create_default_dht_storage(sett));

	span<char const> buf1 = {"123", 3};
	span<char const> buf2 = {"1234", 4};
	sha1_hash const t1 = item_target_id(buf1);
	sha1_hash const t2 = item_target_id(buf2);

	s->put_immutable_item(t1, buf1, addr("124.31.75.21"));
	s->put_immutable_item(t2, buf2, addr("124.31.75.21"));

	public_key pk;
	signature sig;

	span<char const> buf3 = {"12345", 5};
	span<char const> buf4 = {"123456", 6};
	span<char const> salt1 = {"salt1", 5};
	span<char const> salt2 = {"salt2", 5};
	sha1_hash const t3 = item_target_id(salt1, pk);
	sha1_hash const t4 = item_target_id(salt2, pk);

	s->put_mutable_item(t3, buf3, sig, sequence_number(1), pk
	, salt1, addr("124.31.75.21"));
	s->put_mutable_item(t4, buf4, sig, sequence_number(1), pk
	, salt2, addr("124.31.75.21"));

	TEST_EQUAL(s->counters().immutable_data, 2);
	TEST_EQUAL(s->counters().mutable_data, 2);

	dht_storage_items items = test_save_expiration(hours(1), s);
	TEST_EQUAL(items.immutables.size(), 2);
	TEST_EQUAL(items.mutables.size(), 2);
	auto const diff_time = system_clock::now() -
		system_clock::from_time_t(items.immutables[0].last_seen);
	TEST_CHECK(diff_time > hours(1));

	auto substract_hours = [](dht_immutable_data& d, int n)
	{
		d.last_seen = system_clock::to_time_t(
			system_clock::from_time_t(d.last_seen) - hours(n));
	};

	sett.item_lifetime = int(total_seconds(hours(2)));
	substract_hours(items.immutables[0], 1);
	substract_hours(items.immutables[1], 1);
	substract_hours(items.mutables[0], 1);
	substract_hours(items.mutables[1], 1);
	std::unique_ptr<dht_storage_interface> s1(create_default_dht_storage(sett, std::move(items)));
	TEST_EQUAL(s1->counters().immutable_data, 0);
	TEST_EQUAL(s1->counters().mutable_data, 0);
}

#endif // TORRENT_DISABLE_DHT
