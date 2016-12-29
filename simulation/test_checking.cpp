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

#include "libtorrent/session.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/torrent_status.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "test.hpp"
#include "settings.hpp"
#include "create_torrent.hpp"
#include "utils.hpp"

template <typename Setup, typename Test>
void run_test(Setup const& setup, Test const& test)
{
	// this is a seeding torrent
	lt::add_torrent_params atp = create_torrent(0, true);

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	auto ios = std::unique_ptr<sim::asio::io_service>(new sim::asio::io_service(
		sim, lt::address_v4::from_string("50.0.0.1")));
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	setup(atp, pack);

	auto ses = std::make_shared<lt::session>(pack, *ios);

	ses->async_add_torrent(atp);

	print_alerts(*ses);

	sim::timer t(sim, lt::seconds(6), [&](boost::system::error_code const&)
	{
		test(*ses);

		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

TORRENT_TEST(checking)
{
	run_test(
		[](lt::add_torrent_params& atp, lt::settings_pack& p) {
			atp.flags |= lt::torrent_flags::auto_managed;
#ifndef TORRENT_NO_DEPRECATE
			p.set_int(lt::settings_pack::cache_size, 100);
#endif
		},
		[](lt::session& ses) {

			std::vector<lt::torrent_handle> tor = ses.get_torrents();
			TEST_EQUAL(tor.size(), 1);

			TEST_EQUAL(tor[0].status().is_seeding, true);
		});
}

