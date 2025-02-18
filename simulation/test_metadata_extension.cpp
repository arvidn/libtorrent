/*

Copyright (c) 2015-2018, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
	readd = 32,

	// token limit is too low
	token_limit = 64,
};

void run_metadata_test(int flags)
{
	int metadata_alerts = 0;
	int metadata_failed_alerts = 0;

	sim::default_config cfg;
	sim::simulation sim{cfg};

	lt::settings_pack default_settings = settings();

	if (flags & full_encryption)
		enable_enc(default_settings);

	if (flags & utp)
		utp_only(default_settings);

	if (flags & token_limit)
		default_settings.set_int(settings_pack::metadata_token_limit, 10);


	lt::add_torrent_params default_add_torrent;
	if (flags & upload_only)
	{
		default_add_torrent.flags |= torrent_flags::upload_mode;
	}

#if TORRENT_ABI_VERSION < 4
	std::shared_ptr<lt::torrent_info> ti;
#else
	std::shared_ptr<lt::torrent_info const> ti;
#endif

	// TODO: we use real_disk here because the test disk io doesn't support
	// multiple torrents, and readd will add back the same torrent before the
	// first one is done being removed
	setup_swarm(2, ((flags & reverse) ? swarm_test::upload : swarm_test::download)
		| ((flags & readd) ? swarm_test::real_disk : swarm_test_t{})
		, sim
		, default_settings
		, default_add_torrent
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [&ti](lt::add_torrent_params& params) {
			// we want to add the torrent via magnet link
			error_code ec;
			add_torrent_params const p = parse_magnet_uri(
				lt::make_magnet_uri(params), ec);
			TEST_CHECK(!ec);
			ti = params.ti;
			params.ti.reset();
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

			if (alert_cast<metadata_failed_alert>(a))
			{
				metadata_failed_alerts += 1;
			}
			else if (alert_cast<metadata_received_alert>(a))
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
			if ((flags & token_limit) && metadata_failed_alerts > 0)
			{
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

	if (flags & token_limit)
	{
		TEST_EQUAL(metadata_failed_alerts, 1);
	}
	else
	{
		TEST_EQUAL(metadata_alerts, 1);
	}
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

TORRENT_TEST(ut_metadata_token_limit)
{
	run_metadata_test(token_limit);
}

#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_DISABLE_EXTENSIONS
