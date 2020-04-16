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

#include "setup_swarm.hpp"
#include "test.hpp"
#include "create_torrent.hpp"
#include "bittorrent_peer.hpp"
#include "settings.hpp"
#include "utils.hpp"
#include "simulator/utils.hpp"
#include "setup_transfer.hpp" // for addr()

#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/deadline_timer.hpp"

#include <memory>

using namespace lt;

struct choke_state
{
	choke_state() : unchoke_duration(lt::seconds(0)), choked(true) {}
	lt::time_duration unchoke_duration;
	lt::time_point last_unchoke;
	bool choked;
};

TORRENT_TEST(optimistic_unchoke)
{
	int const num_nodes = 20;
	lt::time_duration const test_duration
		= lt::seconds(num_nodes * 90);

	dsl_config network_cfg;
	sim::simulation sim{network_cfg};

	io_service ios(sim, addr("50.1.0.0"));
	lt::time_point start_time(lt::clock_type::now());

	lt::add_torrent_params atp = ::create_torrent(0);
	atp.flags &= ~torrent_flags::auto_managed;
	atp.flags &= ~torrent_flags::paused;

	lt::settings_pack pack = settings();
	// only allow an optimistic unchoke slot
	pack.set_int(settings_pack::unchoke_slots_limit, 1);
	pack.set_int(settings_pack::num_optimistic_unchoke_slots, 1);
	pack.set_int(settings_pack::peer_timeout, 9999);

	std::vector<choke_state> peer_choke_state(num_nodes);

	session_proxy proxy;

	auto ses = std::make_shared<lt::session>(std::ref(pack), std::ref(ios));
	ses->async_add_torrent(atp);

	std::vector<std::shared_ptr<sim::asio::io_service>> io_service;
	std::vector<std::shared_ptr<peer_conn>> peers;

	print_alerts(*ses);

	sim::timer t(sim, lt::seconds(0), [&](boost::system::error_code const&)
	{
		for (int i = 0; i < num_nodes; ++i)
		{
			// create a new io_service
			char ep[30];
			std::snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
			io_service.push_back(std::make_shared<sim::asio::io_service>(
				std::ref(sim), addr(ep)));
			peers.push_back(std::make_shared<peer_conn>(std::ref(*io_service.back())
				, [&,i](int msg, char const* /* buf */, int /* len */)
				{
					choke_state& cs = peer_choke_state[i];
					if (msg == 0)
					{
						// choke
						if (!cs.choked)
						{
							cs.choked = true;
							cs.unchoke_duration += lt::clock_type::now() - cs.last_unchoke;
						}
					}
					else if (msg == 1)
					{
						// unchoke
						if (cs.choked)
						{
							cs.choked = false;
							cs.last_unchoke = lt::clock_type::now();
						}
					}
					else
					{
						return;
					}

					char const* msg_str[] = {"choke", "unchoke"};

					lt::time_duration d = lt::clock_type::now() - start_time;
					std::uint32_t const millis = std::uint32_t(
						lt::duration_cast<lt::milliseconds>(d).count());
					printf("\x1b[35m%4d.%03d: [%d] %s (%d ms)\x1b[0m\n"
						, millis / 1000, millis % 1000, i, msg_str[msg]
						, int(lt::duration_cast<lt::milliseconds>(cs.unchoke_duration).count()));
				}
				, *atp.ti
				, tcp::endpoint(addr("50.1.0.0"), 6881)
				, peer_conn::peer_mode_t::idle));
		}
	});

	sim::timer t2(sim, test_duration, [&](boost::system::error_code const&)
	{
		for (auto& p : peers)
		{
			p->abort();
		}
		proxy = ses->abort();
		ses.reset();
	});

	sim.run();

	std::int64_t const duration_ms = lt::duration_cast<lt::milliseconds>(test_duration).count();
	std::int64_t const average_unchoke_time = duration_ms / num_nodes;
	printf("EXPECT: %" PRId64 " ms\n", average_unchoke_time);
	for (auto& cs : peer_choke_state)
	{
		if (!cs.choked)
		{
			cs.choked = true;
			cs.unchoke_duration += lt::clock_type::now() - cs.last_unchoke;
		}
		std::int64_t const unchoke_duration = lt::duration_cast<lt::milliseconds>(cs.unchoke_duration).count();
		printf("%" PRId64 " ms\n", unchoke_duration);
		TEST_CHECK(std::abs(unchoke_duration - average_unchoke_time) < 1500);
	}
}
