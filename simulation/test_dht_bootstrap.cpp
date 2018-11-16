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
	pack.set_bool(lt::settings_pack::enable_lsd, false);
	pack.set_bool(lt::settings_pack::enable_upnp, false);
	pack.set_bool(lt::settings_pack::enable_natpmp, false);
	pack.set_bool(lt::settings_pack::enable_dht, true);
	sim::asio::io_service ios(sim, addr("10.0.0.1"));
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, ios);

	lt::deadline_timer timer(ios);
	timer.expires_from_now(lt::seconds(10));
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
