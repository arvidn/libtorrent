/*

Copyright (c) 2017, Arvid Norberg
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
#include "utils.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/alert_types.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp" // for timer
#include "settings.hpp"
#include "create_torrent.hpp"

using namespace libtorrent;

TORRENT_TEST(seed_mode)
{
	// with seed mode
	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {
			// make sure the session works with a tick interval of 5 seconds
			pack.set_int(settings_pack::tick_interval, 5000);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= add_torrent_params::flag_seed_mode;
		}
		// on alert
		, [](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool {
			// we don't need to finish seeding, exit after 20 seconds
			return ticks > 20;
		});
}

TORRENT_TEST(force_proxy)
{
	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_service> ios{new sim::asio::io_service(sim
		, address_v4::from_string("50.0.0.1"))};
	lt::session_proxy zombie;

	lt::settings_pack pack = settings();
	pack.set_bool(settings_pack::force_proxy, true);
	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);

	// disable force proxy in 3 seconds (this should make us open up listen
	// sockets)
	sim::timer t1(sim, lt::seconds(3), [&](boost::system::error_code const& ec)
	{
		lt::settings_pack p;
		p.set_bool(settings_pack::force_proxy, false);
		ses->apply_settings(p);
	});

	int num_listen_tcp = 0;
	int num_listen_udp = 0;
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		if (auto la = alert_cast<listen_succeeded_alert>(a))
		{
			if (la->sock_type == listen_succeeded_alert::tcp)
				++num_listen_tcp;
			else if (la->sock_type == listen_succeeded_alert::udp)
				++num_listen_udp;
		}
	});

	// run for 10 seconds.
	sim::timer t2(sim, lt::seconds(10), [&](boost::system::error_code const& ec)
	{
		fprintf(stderr, "shutting down\n");
		// shut down
		zombie = ses->abort();
		ses.reset();
	});
	sim.run();

	TEST_EQUAL(num_listen_tcp, 1);
	TEST_EQUAL(num_listen_udp, 2);
}

