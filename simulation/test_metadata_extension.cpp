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

#include "test.hpp"
#include "test_utils.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"

using namespace lt;

#ifndef TORRENT_DISABLE_EXTENSIONS

enum flags_t
{
	// disconnect immediately after receiving the metadata (to test that
	// edge case, it caused a crash once)
	disconnect = 1,

	// force encryption (to make sure the plugin uses the peer_connection
	// API in a compatible way)
	full_encryption = 2,

	// have the downloader connect to the seeder
	// (instead of the other way around)
	reverse = 4,

	// only use uTP
	utp = 8,

	// upload-only mode
	upload_only = 16,

	// re-add the torrent after removing
	readd = 32
};

void run_metadata_test(int flags)
{
	int metadata_alerts = 0;

	sim::default_config cfg;
	sim::simulation sim{cfg};

	lt::settings_pack default_settings = settings();

	if (flags & full_encryption)
	{
		enable_enc(default_settings);
	}

	if (flags & utp)
	{
		utp_only(default_settings);
	}

	lt::add_torrent_params default_add_torrent;
	if (flags & upload_only)
	{
		default_add_torrent.flags |= torrent_flags::upload_mode;
	}

	std::shared_ptr<lt::torrent_info> ti;

	setup_swarm(2, (flags & reverse) ? swarm_test::upload : swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [&ti](lt::add_torrent_params& params) {
			// we want to add the torrent via magnet link
			error_code ec;
			ti = params.ti;
			params.ti.reset();
			add_torrent_params const p = parse_magnet_uri(
				lt::make_magnet_uri(*ti), ec);
			TEST_CHECK(!ec);
			params.name = p.name;
			params.trackers = p.trackers;
			params.tracker_tiers = p.tracker_tiers;
			params.url_seeds = p.url_seeds;
			params.info_hashes = p.info_hashes;
			params.peers = p.peers;
#ifndef TORRENT_DISABLE_DHT
			params.dht_nodes = p.dht_nodes;
#endif
			params.flags &= ~torrent_flags::upload_mode;
		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {

			if (alert_cast<metadata_received_alert>(a))
			{
				metadata_alerts += 1;

				if (flags & disconnect)
				{
					ses.remove_torrent(ses.get_torrents()[0]);
				}

				if (flags & readd)
				{
					add_torrent_params p = default_add_torrent;
					p.ti = ti;
					p.save_path = ".";
					ses.add_torrent(p);
				}
			}
		}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (flags & reverse)
			{
				return true;
			}

			if (ticks > 70)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if ((flags & disconnect) && metadata_alerts > 0)
			{
				return true;
			}
			if ((flags & upload_only) && has_metadata(ses))
			{
				// the other peer is in upload mode and should not have sent any
				// actual payload to us
				TEST_CHECK(!is_seed(ses));
				return true;
			}

			if (is_seed(ses))
			{
				TEST_CHECK((flags & upload_only) == 0);
				return true;
			}

			return false;
		});

	TEST_EQUAL(metadata_alerts, 1);
}

TORRENT_TEST(ut_metadata_encryption_reverse)
{
	run_metadata_test(full_encryption | reverse);
}

TORRENT_TEST(ut_metadata_encryption_utp)
{
	run_metadata_test(full_encryption | utp);
}

TORRENT_TEST(ut_metadata_reverse)
{
	run_metadata_test(reverse);
}

TORRENT_TEST(ut_metadata_upload_only)
{
	run_metadata_test(upload_only);
}

TORRENT_TEST(ut_metadata_disconnect)
{
	run_metadata_test(disconnect);
}

TORRENT_TEST(ut_metadata_disconnect_readd)
{
	run_metadata_test(disconnect | readd);
}

TORRENT_TEST(ut_metadata_upload_only_disconnect_readd)
{
	run_metadata_test(upload_only | disconnect | readd);
}
#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_DISABLE_EXTENSIONS
