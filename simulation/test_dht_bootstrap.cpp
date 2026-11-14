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
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/session_params.hpp"

using namespace sim;

#ifndef TORRENT_DISABLE_DHT

namespace {
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
} // anonymous namespace

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

#if TORRENT_USE_I2P
namespace {

lt::add_torrent_params make_torrent(char const* tracker)
{
	std::vector<lt::create_file_entry> files;
	files.emplace_back("foobar", 13241);
	lt::create_torrent ct(std::move(files));
	ct.add_tracker(tracker);
	ct.add_node({"10.0.0.20", 6881});

	for (lt::piece_index_t i(0); i < lt::piece_index_t(ct.num_pieces()); ++i)
		ct.set_hash(i, lt::sha1_hash::max());

	return lt::load_torrent_buffer(lt::bencode(ct.generate()));
}

// returns whether the DHT node listed in the torrent was contacted
bool test_torrent_dht_nodes(lt::add_torrent_params atp, bool const allow_i2p_mixed)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	std::vector<lt::session_proxy> zombies;

	fake_node node(sim, "10.0.0.20", 6881);

	lt::settings_pack pack;
	pack.set_int(lt::settings_pack::aio_threads, 0);
	pack.set_int(lt::settings_pack::hashing_threads, 0);
	pack.set_bool(lt::settings_pack::enable_lsd, false);
	pack.set_bool(lt::settings_pack::enable_upnp, false);
	pack.set_bool(lt::settings_pack::enable_natpmp, false);
	pack.set_bool(lt::settings_pack::enable_dht, true);
	// the only node the DHT may learn about is the one in the torrent
	pack.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
	pack.set_bool(lt::settings_pack::allow_i2p_mixed, allow_i2p_mixed);
	sim::asio::io_context ios(sim, addr("10.0.0.1"));
	lt::session_params sp(pack);
	sp.disk_io_constructor = lt::disabled_disk_io_constructor;
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, ios);

	atp.save_path = ".";
	atp.flags |= lt::torrent_flags::paused;
	atp.flags &= ~lt::torrent_flags::auto_managed;
	ses->async_add_torrent(std::move(atp));

	lt::aux::deadline_timer timer(ios);
	timer.expires_after(lt::seconds(10));
	timer.async_wait([&](lt::error_code const&) {
		zombies.push_back(ses->abort());
		node.close();
		ses.reset();
	});

	print_alerts(*ses);

	sim.run();

	return node.tripped();
}

} // anonymous namespace

TORRENT_TEST(torrent_dht_nodes_regular)
{
	lt::add_torrent_params atp = make_torrent("http://tracker.com:8080/announce");
	TEST_CHECK(test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(torrent_dht_nodes_i2p_tracker)
{
	lt::add_torrent_params atp = make_torrent("http://tracker.i2p/announce");
	TEST_CHECK(atp.flags & lt::torrent_flags::i2p_torrent);
	TEST_CHECK(!test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(torrent_dht_nodes_i2p_tracker_mixed)
{
	lt::add_torrent_params atp = make_torrent("http://tracker.i2p/announce");
	TEST_CHECK(test_torrent_dht_nodes(std::move(atp), true));
}

TORRENT_TEST(atp_dht_nodes_regular)
{
	lt::add_torrent_params atp;
	atp.info_hashes.v1 = lt::sha1_hash("abababababababababab");
	atp.dht_nodes.emplace_back("10.0.0.20", 6881);
	TEST_CHECK(test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(atp_dht_nodes_i2p_flag)
{
	lt::add_torrent_params atp;
	atp.info_hashes.v1 = lt::sha1_hash("abababababababababab");
	atp.dht_nodes.emplace_back("10.0.0.20", 6881);
	atp.flags |= lt::torrent_flags::i2p_torrent;
	TEST_CHECK(!test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(torrent_dht_nodes_i2p_flag)
{
	// the i2p_torrent flag alone, the torrent file has a regular tracker
	lt::add_torrent_params atp = make_torrent("http://tracker.com:8080/announce");
	TEST_CHECK(!(atp.flags & lt::torrent_flags::i2p_torrent));
	atp.flags |= lt::torrent_flags::i2p_torrent;
	TEST_CHECK(!test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(atp_dht_nodes_i2p_flag_mixed)
{
	lt::add_torrent_params atp;
	atp.info_hashes.v1 = lt::sha1_hash("abababababababababab");
	atp.dht_nodes.emplace_back("10.0.0.20", 6881);
	atp.flags |= lt::torrent_flags::i2p_torrent;
	TEST_CHECK(test_torrent_dht_nodes(std::move(atp), true));
}
#endif // TORRENT_USE_I2P

#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_DISABLE_DHT
