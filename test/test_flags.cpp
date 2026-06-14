/*

Copyright (c) 2017, AllSeeingEyeTolledEweSew
Copyright (c) 2017, 2019-2020, 2022, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "settings.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
using namespace std::chrono_literals;
namespace lt = libtorrent;

namespace {

std::string file(std::string const& name)
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
	add_torrent_params p = load_torrent_file(file("base.torrent"));
	p.save_path = ".";
	if (flags & (torrent_flags::seed_mode | torrent_flags::super_seeding))
	{
		std::vector<char> temp(425);
		ofstream("temp").write(temp.data(), std::streamsize(temp.size()));
	}
	p.flags = flags;
	const torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.flags() & flags, flags);
	print_alerts(ses);
}

void test_set_after_add(torrent_flags_t const flags)
{
	session ses(settings());
	add_torrent_params p = load_torrent_file(file("base.torrent"));
	p.save_path = ".";
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
	add_torrent_params p = load_torrent_file(file("base.torrent"));
	p.save_path = ".";
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

// stop_when_ready force-stops the torrent once it becomes ready to start
// downloading (transitions into a downloading state) and clears the flag.
//
// The torrent is added paused with a partial have_pieces bitmask, which keeps
// it in the checking_files state (not a downloading state, so the flag is
// preserved) and lets us observe the flag set. Resuming then lets checking
// complete, which makes the torrent ready and triggers stop_when_ready.
TORRENT_TEST(flag_stop_when_ready)
{
	// a multi-piece torrent is required to be able to resume mid-check
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("temp", std::int64_t(default_block_size) * 4);
	lt::create_torrent ct(std::move(fs), default_block_size, lt::create_torrent::v1_only);
	for (auto const i : ct.piece_range())
		ct.set_hash(i, sha1_hash::max());

	session ses(settings());
	add_torrent_params p = load_torrent_buffer(bencode(ct.generate()));
	p.save_path = ".";
	p.flags = torrent_flags::stop_when_ready | torrent_flags::paused;
	// a partial have_pieces bitmask makes the torrent resume mid-check, so it
	// stays in the checking_files state rather than becoming ready
	p.have_pieces.resize(1, false);
	const torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_EQUAL(h.flags() & torrent_flags::stop_when_ready, torrent_flags::stop_when_ready);

	h.resume();

	// wait for checking to complete, which makes the torrent ready and
	// transitions it into a downloading state. status() is serialized after
	// that transition on the network thread, so by the time we read it the
	// torrent has been force-stopped: paused and no longer auto-managed, in a
	// downloading state, with stop_when_ready cleared.
	const bool ready = wait_for_alert(
		ses,
		"flag_stop_when_ready",
		[](lt::alert const* a) {
			auto const* sc = alert_cast<state_changed_alert>(a);
			return sc && sc->state == torrent_status::downloading;
		},
		30s);
	TEST_CHECK(ready);

	const torrent_status st = h.status();
	TEST_EQUAL(st.flags & torrent_flags::stop_when_ready, torrent_flags_t{});
	TEST_EQUAL(st.flags & torrent_flags::paused, torrent_flags::paused);
	TEST_EQUAL(st.flags & torrent_flags::auto_managed, torrent_flags_t{});
	TEST_EQUAL(st.state, torrent_status::downloading);
	print_alerts(ses);
}

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
