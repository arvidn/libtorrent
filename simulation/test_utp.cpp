/*

Copyright (c) 2008, Arvid Norberg
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
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/time.hpp" // for clock_type

#include "test.hpp"
#include "setup_swarm.hpp"
#include "settings.hpp"
#include <fstream>
#include <iostream>

using namespace lt;

TORRENT_TEST(utp)
{
	// TODO: 3 simulate packet loss
	// TODO: 3 simulate unpredictible latencies
	// TODO: 3 simulate proper (taildrop) queues (perhaps even RED and BLUE)
	lt::time_point start_time = lt::clock_type::now();

	sim::default_config cfg;
	sim::simulation sim{cfg};

	setup_swarm(2, swarm_test::upload, sim
		// add session
		, [](lt::settings_pack& pack) {
		// force uTP connection
			utp_only(pack);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
		}
		// on alert
		, [&](lt::alert const*, lt::session& ses) {
			if (!is_seed(ses)) return;

			// if this check fails, there is a performance regression in the protocol,
			// either uTP or bittorrent itself. Be careful with the download request
			// queue size (and make sure it can grow fast enough, to keep up with
			// slow-start) and the send buffer watermark
			TEST_CHECK(lt::clock_type::now() < start_time + lt::milliseconds(8500));
		}
		// terminate
		, [](int const ticks, lt::session&) -> bool
		{
			if (ticks > 100)
			{
				TEST_ERROR("timeout");
				return true;
			}
			return false;
		});
}

