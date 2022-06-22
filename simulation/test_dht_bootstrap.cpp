/*

Copyright (c) 2016, 2021, Alden Torres
Copyright (c) 2016, 2018-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "simulator/simulator.hpp"
#include "utils.hpp"
#include "fake_peer.hpp" // for fake_node
#include "libtorrent/time.hpp"
#include "settings.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "setup_transfer.hpp" // for addr()

using namespace sim;

#ifndef TORRENT_DISABLE_DHT

struct sim_config : sim::default_config
{
	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec)
	{
		if (hostname == "dht.libtorrent.org")
		{
			result.push_back(addr("10.0.0.10"));
			return lt::duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}
		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

TORRENT_TEST(dht_bootstrap)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	std::vector<lt::session_proxy> zombies;

	fake_node node(sim, "10.0.0.10", 25401);

	lt::settings_pack pack;
	// we use 0 threads (disk I/O operations will be performed in the network
	// thread) to be simulator friendly.
	pack.set_int(lt::settings_pack::aio_threads, 0);
	pack.set_int(lt::settings_pack::hashing_threads, 0);
	pack.set_bool(lt::settings_pack::enable_lsd, false);
	pack.set_bool(lt::settings_pack::enable_upnp, false);
	pack.set_bool(lt::settings_pack::enable_natpmp, false);
	pack.set_bool(lt::settings_pack::enable_dht, true);
	sim::asio::io_context ios(sim, addr("10.0.0.1"));
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, ios);

	lt::aux::deadline_timer timer(ios);
	timer.expires_after(lt::seconds(10));
	timer.async_wait([&](lt::error_code const&) {
		zombies.push_back(ses->abort());
		node.close();
		ses.reset();
	});

	print_alerts(*ses);

	sim.run();

	TEST_EQUAL(node.tripped(), true);

	std::vector<char> const& p = node.incoming_packets().front();
	lt::bdecode_node n;
	boost::system::error_code err;
	int const ret = bdecode(p.data(), p.data() + p.size()
		, n, err, nullptr, 10, 200);
	TEST_EQUAL(ret, 0);

	lt::bdecode_node a = n.dict_find_dict("a");
	TEST_CHECK(a.dict_find_int_value("bs", -1) == 1);
}

#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_DISABLE_DHT
