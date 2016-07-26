/*

Copyright (c) 2008-2015, Arvid Norberg
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

#ifndef TORRENT_NO_DEPRECATE

#include "setup_swarm.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/extensions/lt_trackers.hpp"
#include "libtorrent/session.hpp"
#include "settings.hpp"

using namespace libtorrent;

TORRENT_TEST(plain)
{
	dsl_config network_cfg;
	sim::simulation sim{network_cfg};

	settings_pack pack = settings();

	add_torrent_params p;
	p.flags &= ~lt::add_torrent_params::flag_paused;
	p.flags &= ~lt::add_torrent_params::flag_auto_managed;

	// the default torrent has one tracker
	// we remove this from session 0 (the one under test)
	p.trackers.push_back("http://test.non-existent.com/announce");

	bool connected = false;

	setup_swarm(2, swarm_test::upload
		, sim , pack, p
		// init session
		, [](lt::session& ses) {
			ses.add_extension(&create_lt_trackers_plugin);
		}
		// add session
		, [](lt::settings_pack& pack) {}
		// add torrent
		, [](lt::add_torrent_params& params) {

			// make sure neither peer has any content
			// TODO: it would be more efficient to not create the content in the first
			// place
			params.save_path = save_path(test_counter(), 1);

			// the test is whether this peer will receive the tracker or not
			params.trackers.clear();
		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {
			if (alert_cast<lt::peer_connect_alert>(a))
				connected = true;
		}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool {
			if (ticks > 10)
			{
				TEST_ERROR("timeout");
				return true;
			}
			return connected && ses.get_torrents()[0].trackers().size() > 0;
		});

	TEST_EQUAL(connected, true);
}

TORRENT_TEST(no_metadata)
{
	dsl_config network_cfg;
	sim::simulation sim{network_cfg};

	settings_pack pack = settings();

	add_torrent_params p;
	p.flags &= ~lt::add_torrent_params::flag_paused;
	p.flags &= ~lt::add_torrent_params::flag_auto_managed;

	// the default torrent has one tracker
	// we remove this from session 0 (the one under test)
	p.trackers.push_back("http://test.non-existent.com/announce");

	bool connected = false;

	setup_swarm(2, swarm_test::upload
		, sim , pack, p
		// init session
		, [](lt::session& ses) {
			ses.add_extension(&create_lt_trackers_plugin);
		}
		// add session
		, [](lt::settings_pack& pack) {}
		// add torrent
		, [](lt::add_torrent_params& params) {

			// make sure neither peer has any content
			// TODO: it would be more efficient to not create the content in the first
			// place
			params.save_path = save_path(test_counter(), 1);

			// the test is whether this peer will receive the tracker or not
			params.trackers.clear();

			// if we don't have metadata, the other peer should not send the
			// tracker to us
			params.info_hash = sha1_hash("aaaaaaaaaaaaaaaaaaaa");
			params.ti.reset();
		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {
			if (alert_cast<lt::peer_connect_alert>(a))
				connected = true;
		}
		// terminate
		, [](int ticks, lt::session& ses) -> bool {
			if (ticks < 10)
				return false;
			TEST_EQUAL(ses.get_torrents()[0].trackers().size(), 0);
			return true;
		});

	TEST_EQUAL(connected, true);
}
#else
TORRENT_TEST(dummy) {}
#endif

