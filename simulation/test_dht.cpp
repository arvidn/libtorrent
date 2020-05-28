/*

Copyright (c) 2015, Arvid Norberg
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
#include "setup_dht.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/socket_io.hpp"
#include "setup_swarm.hpp"
#include "libtorrent/kademlia/ed25519.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/kademlia/item.hpp"
#include "libtorrent/kademlia/dht_state.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"

using lt::settings_pack;

#ifndef TORRENT_DISABLE_DHT
void bootstrap_session(std::vector<dht_network*> networks, lt::session& ses)
{
	lt::dht::dht_state state;

	for (auto dht : networks)
	{
		// bootstrap off of 8 of the nodes
		auto router_nodes = dht->router_nodes();
		if (lt::aux::is_v6(router_nodes.front()))
			state.nodes6 = router_nodes;
		else
			state.nodes = router_nodes;
	}

	ses.set_dht_state(std::move(state));
	settings_pack pack;
	pack.set_bool(settings_pack::enable_dht, true);
	pack.set_int(settings_pack::alert_mask, lt::alert_category::all);
	pack.set_bool(settings_pack::dht_ignore_dark_internet, false);
	pack.set_bool(settings_pack::dht_restrict_routing_ips, false);
	ses.apply_settings(pack);
}
#endif // TORRENT_DISABLE_DHT

TORRENT_TEST(dht_bootstrap)
{
#ifndef TORRENT_DISABLE_DHT
	sim::default_config cfg;
	sim::simulation sim{cfg};

	dht_network dht(sim, 3000);

	int routing_table_depth = 0;
	int num_nodes = 0;

	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&)
		{
			if (lt::dht_stats_alert const* p = lt::alert_cast<lt::dht_stats_alert>(a))
			{
				routing_table_depth = std::max(int(p->routing_table.size()), routing_table_depth);
				int c = 0;
				for (auto const& b : p->routing_table)
				{
					c += b.num_nodes;
					c += b.num_replacements;
				}
				num_nodes = std::max(c, num_nodes);
				print_routing_table(p->routing_table);
			}
			else if (lt::session_stats_alert const* sa = lt::alert_cast<lt::session_stats_alert>(a))
			{
				int const dht_nodes = lt::find_metric_idx("dht.nodes");
				TEST_CHECK(sa->counters()[dht_nodes] > 2);
			}
		}
		// terminate?
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (ticks == 0)
			{
				bootstrap_session({&dht}, ses);
			}
			if (ticks > 500)
			{
				ses.post_session_stats();
				std::printf("depth: %d nodes: %d\n", routing_table_depth, num_nodes);
				TEST_CHECK(routing_table_depth >= 8);
				TEST_CHECK(num_nodes >= 50);
				dht.stop();
				return true;
			}
			ses.post_dht_stats();
			return false;
		});

	sim.run();

#endif // TORRENT_DISABLE_DHT

}

TORRENT_TEST(dht_dual_stack_get_peers)
{
#ifndef TORRENT_DISABLE_DHT
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	dht_network dht(sim, 100);
	dht_network dht6(sim, 100, dht_network::bind_ipv6);

	lt::sha1_hash const test_ih("01234567890123456789");
	bool got_peer_v4 = false, got_peer_v6 = false;

	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](lt::settings_pack&) {
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&)
		{
			if (lt::dht_get_peers_reply_alert const* p = lt::alert_cast<lt::dht_get_peers_reply_alert>(a))
			{
				std::vector<lt::tcp::endpoint> peers = p->peers();
				for (lt::tcp::endpoint const& peer : peers)
				{
					// TODO: verify that the endpoint matches the session's
					got_peer_v4 |= lt::aux::is_v4(peer);
					got_peer_v6 |= lt::aux::is_v6(peer);
				}
			}
		}
		// terminate?
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (ticks == 0)
			{
				bootstrap_session({&dht, &dht6}, ses);
			}
			if (ticks == 2)
			{
				ses.dht_announce(test_ih, 6881);
			}
			if (ticks == 4)
			{
				ses.dht_get_peers(test_ih);
			}
			if (ticks == 6)
			{
				TEST_CHECK(got_peer_v4);
				TEST_CHECK(got_peer_v6);
				return true;
			}
			return false;
		});

	sim.run();

#endif // TORRENT_DISABLE_DHT
}

TORRENT_TEST(dht_dual_stack_immutable_item)
{
#ifndef TORRENT_DISABLE_DHT
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	dht_network dht(sim, 100);
	dht_network dht6(sim, 100, dht_network::bind_ipv6);

	lt::sha1_hash item_hash;
	bool got_item = false;

	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](lt::settings_pack&) {
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&)
		{
			if (lt::dht_immutable_item_alert const* p = lt::alert_cast<lt::dht_immutable_item_alert>(a))
			{
				// we should only get one alert for each request
				TEST_CHECK(!got_item);
				got_item = p->target == item_hash && p->item.string() == "immutable item";
			}
		}
		// terminate?
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (ticks == 0)
			{
				bootstrap_session({&dht, &dht6}, ses);
			}
			if (ticks == 2)
			{
				item_hash = ses.dht_put_item(lt::entry("immutable item"));
			}
			if (ticks == 4)
			{
				ses.dht_get_item(item_hash);
			}
			if (ticks == 6)
			{
				TEST_CHECK(got_item);
				return true;
			}
			return false;
		});

	sim.run();

#endif // TORRENT_DISABLE_DHT
}

TORRENT_TEST(dht_dual_stack_mutable_item)
{
#ifndef TORRENT_DISABLE_DHT
	sim::default_config cfg;
	sim::simulation sim{ cfg };

	dht_network dht(sim, 100);
	dht_network dht6(sim, 100, dht_network::bind_ipv6);

	lt::dht::secret_key sk;
	lt::dht::public_key pk;
	int put_count = 0;
	bool got_item = false;

	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](lt::settings_pack&) {
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&)
		{
			if (lt::dht_mutable_item_alert const* p = lt::alert_cast<lt::dht_mutable_item_alert>(a))
			{
				TEST_CHECK(!got_item);
				if (p->authoritative)
					got_item = p->key == pk.bytes && p->item.string() == "mutable item";
			}
		}
		// terminate?
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (ticks == 0)
			{
				bootstrap_session({&dht, &dht6}, ses);
			}
			if (ticks == 2)
			{
				std::array<char, 32> seed;
				std::tie(pk, sk) = lt::dht::ed25519_create_keypair(seed);

				ses.dht_put_item(pk.bytes, [&](lt::entry& item, std::array<char, 64>& sig
					, std::int64_t& seq, std::string const& salt)
				{
					item = "mutable item";
					seq = 1;
					std::vector<char> v;
					lt::bencode(std::back_inserter(v), item);
					lt::dht::signature sign = lt::dht::sign_mutable_item(v, salt
						, lt::dht::sequence_number(seq), pk, sk);
					put_count++;
					sig = sign.bytes;
				});
			}
			if (ticks == 4)
			{
				// should be one for each stack, ipv4 and ipv6
				TEST_EQUAL(put_count, 2);
				ses.dht_get_item(pk.bytes);
			}
			if (ticks == 6)
			{
				TEST_CHECK(got_item);
				return true;
			}
			return false;
		});

	sim.run();

#endif // TORRENT_DISABLE_DHT
}
