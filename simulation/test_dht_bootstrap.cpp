/*

Copyright (c) 2016, Arvid Norberg
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
#include "simulator/simulator.hpp"
#include "utils.hpp"
#include "fake_peer.hpp" // for fake_node
#include "libtorrent/time.hpp"
#include "settings.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "setup_transfer.hpp" // for addr()
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/session_params.hpp"

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

	lt::deadline_timer timer(ios);
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

std::shared_ptr<lt::torrent_info> make_torrent(char const* tracker)
{
	lt::file_storage fs;
	fs.add_file("foobar", 13241);
	lt::create_torrent ct(fs);
	ct.add_tracker(tracker);
	ct.add_node({"10.0.0.20", 6881});

	for (lt::piece_index_t i(0); i < lt::piece_index_t(ct.num_pieces()); ++i)
		ct.set_hash(i, lt::sha1_hash::max());

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), ct.generate());
	return std::make_shared<lt::torrent_info>(buf, lt::from_span);
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

	lt::deadline_timer timer(ios);
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
	lt::add_torrent_params atp;
	atp.ti = make_torrent("http://tracker.com:8080/announce");
	TEST_CHECK(test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(torrent_dht_nodes_i2p_tracker)
{
	lt::add_torrent_params atp;
	atp.ti = make_torrent("http://tracker.i2p/announce");
	TEST_CHECK(atp.ti->is_i2p());
	TEST_CHECK(!test_torrent_dht_nodes(std::move(atp), false));
}

TORRENT_TEST(torrent_dht_nodes_i2p_tracker_mixed)
{
	lt::add_torrent_params atp;
	atp.ti = make_torrent("http://tracker.i2p/announce");
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
	lt::add_torrent_params atp;
	atp.ti = make_torrent("http://tracker.com:8080/announce");
	atp.flags |= lt::torrent_flags::i2p_torrent;
	TEST_CHECK(!atp.ti->is_i2p());
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
