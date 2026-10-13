/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_handle.hpp"

#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for addr()
#include "utils.hpp"
#include "disk_io.hpp"

using namespace lt;

namespace {

// starts a session configured with the given existing_files_mode, lets the
// initial full check run to completion, and returns how many
// async_hash()/async_hash2() calls the check dispatched.
int run_check(existing_files_mode const mode, int const num_pieces)
{
	lt::address const peer0 = addr("50.0.0.1");

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, peer0};

	lt::session_proxy zombie;

	test_disk cfg = test_disk().set_files(mode);

	lt::session_params params;
	params.settings = settings();
	params.disk_io_constructor = cfg;

	auto ses = std::make_shared<lt::session>(params, ios0);

	// one piece per block, so test_disk's block-granular m_have maps 1:1
	// onto pieces
	lt::add_torrent_params atp =
		::create_test_torrent(lt::default_block_size, num_pieces, lt::create_torrent::v1_only, 1);
	atp.save_path = ".";
	atp.flags &= ~lt::torrent_flags::paused;
	atp.flags &= ~lt::torrent_flags::auto_managed;

	bool checked = false;
	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (alert_cast<torrent_checked_alert>(a))
			checked = true;
	});

	ses->async_add_torrent(atp);

	sim::timer t1(sim, lt::seconds(5), [&](boost::system::error_code const&) {
		TEST_CHECK(checked);
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	return *cfg.num_hash_calls;
}

} // anonymous namespace

// create_test_torrent() pads the requested size by 1234 bytes (less than
// one block), which always adds exactly one extra, smaller, final piece;
// account for that here rather than assume num_pieces is exact.
constexpr int requested_pieces = 8;
constexpr int total_pieces = requested_pieces + 1;

// nothing exists on disk and there's no resume data: async_check_files()
// never reports need_full_check (mirroring has_any_file() in the real
// backends), so start_checking() never runs at all. This does not exercise
// the per-piece skip (m_checkable_pieces); it only confirms the check
// short-circuits to zero hash calls when there's truly nothing to check.
TORRENT_TEST(check_files_skip_no_files)
{
	TEST_EQUAL(run_check(existing_files_mode::no_files, requested_pieces), 0);
}

// everything exists and is correct: every piece is still genuinely
// unknown to the proactive check, so the real hash check must run for
// every one of them.
TORRENT_TEST(check_files_skip_full_valid)
{
	TEST_EQUAL(run_check(existing_files_mode::full_valid, requested_pieces), total_pieces);
}

// only the first half of the data exists on disk: the second half is
// proactively known-absent and must be skipped, while the first half is
// still genuinely hash-checked.
TORRENT_TEST(check_files_skip_partial_valid)
{
	TEST_EQUAL(run_check(existing_files_mode::partial_valid, requested_pieces), total_pieces / 2);
}

// the data exists on disk but is wrong: a real backend would see a
// correctly-sized file and have no reason to skip it, so every piece must
// still be hash-checked (and subsequently fail).
TORRENT_TEST(check_files_skip_full_invalid)
{
	TEST_EQUAL(run_check(existing_files_mode::full_invalid, requested_pieces), total_pieces);
}
