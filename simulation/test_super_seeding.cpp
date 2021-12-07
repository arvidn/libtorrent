/*

Copyright (c) 2014-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "setup_swarm.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "test.hpp"

using namespace lt;

#ifndef TORRENT_DISABLE_SUPERSEEDING
TORRENT_TEST(super_seeding)
{
	setup_swarm(5, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::super_seeding;
		}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int, lt::session&) -> bool
		{ return true; });
}

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(strict_super_seeding)
{
	setup_swarm(5, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_bool(settings_pack::strict_super_seeding, true);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::super_seeding;
		}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int, lt::session&) -> bool
		{ return true; });
}
#endif
#else
TORRENT_TEST(summy) {}
#endif
