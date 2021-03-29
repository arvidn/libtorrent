/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, AllSeeingEyeTolledEweSew
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/path.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"

using namespace libtorrent;

namespace {

std::string file(std::string name)
{
	return combine_path(parent_path(current_working_directory())
		, combine_path("test_torrents", name));
}

void print_alerts(lt::session& ses)
{
	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);
	for (auto a : alerts)
	{
		std::printf("[%s] %s\n", a->what(), a->message().c_str());
	}
}

void test_add_and_get_flags(torrent_flags_t const flags)
{
	session ses(settings());
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.ti = std::make_shared<torrent_info>(file("base.torrent"),
		std::ref(ec));
	if (flags & torrent_flags::seed_mode)
	{
		std::vector<char> temp(425);
		ofstream("temp").write(temp.data(), std::streamsize(temp.size()));
	}
	TEST_CHECK(!ec);
	p.flags = flags;
	const torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.flags() & flags, flags);
	print_alerts(ses);
}

void test_set_after_add(torrent_flags_t const flags)
{
	session ses(settings());
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.ti = std::make_shared<torrent_info>(file("base.torrent"),
		std::ref(ec));
	TEST_CHECK(!ec);
	p.flags = torrent_flags::all & ~flags;
	const torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.flags() & flags, torrent_flags_t{});
	h.set_flags(flags);
	TEST_EQUAL(h.flags() & flags, flags);
	print_alerts(ses);
}

void test_unset_after_add(torrent_flags_t const flags)
{
	session ses(settings());
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.ti = std::make_shared<torrent_info>(file("base.torrent"),
		std::ref(ec));
	TEST_CHECK(!ec);
	p.flags = flags;
	const torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.flags() & flags, flags);
	h.unset_flags(flags);
	TEST_EQUAL(h.flags() & flags, torrent_flags_t{});
	print_alerts(ses);
}

} // anonymous namespace

TORRENT_TEST(flag_seed_mode)
{
	// seed-mode (can't be set after adding)
	test_add_and_get_flags(torrent_flags::seed_mode);
	test_unset_after_add(torrent_flags::seed_mode);
}

TORRENT_TEST(flag_upload_mode)
{
	// upload-mode
	test_add_and_get_flags(torrent_flags::upload_mode);
	test_set_after_add(torrent_flags::upload_mode);
	test_unset_after_add(torrent_flags::upload_mode);
}

#ifndef TORRENT_DISABLE_SHARE_MODE
TORRENT_TEST(flag_share_mode)
{
	// share-mode
	test_add_and_get_flags(torrent_flags::share_mode);
	test_set_after_add(torrent_flags::share_mode);
	test_unset_after_add(torrent_flags::share_mode);
}
#endif

TORRENT_TEST(flag_apply_ip_filter)
{
	// apply-ip-filter
	test_add_and_get_flags(torrent_flags::apply_ip_filter);
	test_set_after_add(torrent_flags::apply_ip_filter);
	test_unset_after_add(torrent_flags::apply_ip_filter);
}

TORRENT_TEST(flag_paused)
{
	// paused
	test_add_and_get_flags(torrent_flags::paused);
	// TODO: change to a different test setup. currently always paused.
	//test_set_after_add(torrent_flags::paused);
	//test_unset_after_add(torrent_flags::paused);
}

TORRENT_TEST(flag_auto_managed)
{
	// auto-managed
	test_add_and_get_flags(torrent_flags::auto_managed);
	test_set_after_add(torrent_flags::auto_managed);
	test_unset_after_add(torrent_flags::auto_managed);
}

// super seeding mode is automatically turned off if we're not a seed
// since the posix_disk_io is not threaded, this will happen immediately
#if TORRENT_HAVE_MMAP
#ifndef TORRENT_DISABLE_SUPERSEEDING
TORRENT_TEST(flag_super_seeding)
{
	// super-seeding
	test_add_and_get_flags(torrent_flags::super_seeding);
	test_unset_after_add(torrent_flags::super_seeding);
	test_set_after_add(torrent_flags::super_seeding);
}
#endif
#endif

TORRENT_TEST(flag_sequential_download)
{
	// sequential-download
	test_add_and_get_flags(torrent_flags::sequential_download);
	test_set_after_add(torrent_flags::sequential_download);
	test_unset_after_add(torrent_flags::sequential_download);
}

// the stop when ready flag will be cleared when the torrent is ready to start
// downloading.
// since the posix_disk_io is not threaded, this will happen immediately
#if TORRENT_HAVE_MMAP
TORRENT_TEST(flag_stop_when_ready)
{
	// stop-when-ready
	// TODO: this test is flaky, since the torrent will become ready before
	// asking for the flags, and by then stop_when_ready will have been cleared
	//test_add_and_get_flags(torrent_flags::stop_when_ready);
	// setting stop-when-ready when already stopped has no effect.
	// TODO: change to a different test setup. currently always paused.
	//test_set_after_add(torrent_flags::stop_when_ready);
	test_unset_after_add(torrent_flags::stop_when_ready);
}
#endif

TORRENT_TEST(flag_disable_dht)
{
	test_add_and_get_flags(torrent_flags::disable_dht);
	test_set_after_add(torrent_flags::disable_dht);
	test_unset_after_add(torrent_flags::disable_dht);
}


TORRENT_TEST(flag_disable_lsd)
{
	test_add_and_get_flags(torrent_flags::disable_lsd);
	test_set_after_add(torrent_flags::disable_lsd);
	test_unset_after_add(torrent_flags::disable_lsd);
}

TORRENT_TEST(flag_disable_pex)
{
	test_add_and_get_flags(torrent_flags::disable_pex);
	test_set_after_add(torrent_flags::disable_pex);
	test_unset_after_add(torrent_flags::disable_pex);
}
