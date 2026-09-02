/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "setup_swarm.hpp"
#include "disk_io.hpp"
#include "test.hpp"
#include "settings.hpp"
#include "create_torrent.hpp"
#include "utils.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/torrent_status.hpp"

#include <fstream>
#include <string_view>

using namespace lt;

namespace {

// v2 and v1 log the same "hash1:"/"hash2:" fields, but v2 prints a
// 256-bit sha256 digest (64 hex digits) vs v1's 64-bit siphash digest
// (16 hex digits), so digest length tells them apart.
bool banned_via_v2_mechanism(std::string_view const msg)
{
	auto const pos = msg.find("hash1: ");
	if (pos == std::string_view::npos)
		return false;
	auto const start = pos + std::string_view("hash1: ").size();
	auto const end = msg.find(' ', start);
	auto const len = (end == std::string_view::npos ? msg.size() : end) - start;
	return len == 64;
}

// node 0 downloads a single, 4-block piece from node 1 (a normal seed) and
// node 2 (a seed that sends corrupt data for that piece). A tiny outstanding
// request limit forces both peers to contribute blocks to it, ruling out
// the affinity for requesting a whole piece from one peer. smart_ban must
// attribute the bad block to the right peer regardless of v1/v2, and only
// the corrupt peer should be banned.
void run_bans_correct_peer(lt::create_flags_t const torrent_flags)
{
	bool banned_bad_peer = false;
	bool banned_good_peer = false;
	bool bad_peer_banned_via_v2 = false;

	// smart_ban's read-back attribution (and the merkle per-block
	// short-circuit) only has anything interesting to do if piece 0 was
	// actually assembled from blocks contributed by both peers. if the
	// picker happened to fetch the whole piece from just one of them, the
	// test would pass without exercising the attribution logic at all
	bool good_peer_contributed = false;
	bool bad_peer_contributed = false;

	dsl_config network_cfg;
	sim::simulation sim{network_cfg};

	lt::settings_pack default_settings = settings();

	lt::add_torrent_params default_add_torrent;
	default_add_torrent.flags &= ~lt::torrent_flags::paused;
	default_add_torrent.flags &= ~lt::torrent_flags::auto_managed;

	setup_swarm(
		3,
		swarm_test::download,
		sim,
		default_settings,
		default_add_torrent
		// init session
		,
		[](lt::session&) {} // add session
		,
		[](lt::settings_pack& sett) {
			// don't request a whole piece from a single peer; force blocks
			// of the same piece to be split across both available peers
			sett.set_int(settings_pack::whole_pieces_threshold, 0);
			sett.set_int(settings_pack::max_out_request_queue, 1);
			sett.set_int(settings_pack::alert_mask, alert_category::all);
		}
		// add torrent
		,
		[](lt::add_torrent_params&) {} // on alert
		,
		[&](lt::alert const* a, lt::session&) {
			if (auto const* bf = alert_cast<block_finished_alert>(a))
			{
				if (bf->piece_index != piece_index_t{0})
					return;
				// message() returns by value; keep it alive as a std::string
				// rather than binding a string_view to the temporary
				std::string const msg = bf->message();
				if (msg.find("50.0.0.2") != std::string::npos)
					good_peer_contributed = true;
				if (msg.find("50.0.0.3") != std::string::npos)
					bad_peer_contributed = true;
				return;
			}
			auto const* log = alert_cast<torrent_log_alert>(a);
			if (log == nullptr)
				return;
			std::string_view const msg = log->log_message();
			if (msg.find("BANNING PEER") == std::string_view::npos)
				return;
			if (msg.find("50.0.0.3") != std::string_view::npos)
			{
				banned_bad_peer = true;
				if (banned_via_v2_mechanism(msg))
					bad_peer_banned_via_v2 = true;
			}
			if (msg.find("50.0.0.2") != std::string_view::npos)
				banned_good_peer = true;
		}
		// terminate
		,
		[](int const ticks, lt::session&) -> bool { return ticks > 30; }
		// customize_disk: node 2 sends corrupt data, restricted to piece 0;
		// corrupting the padding trailing piece too would get node 2
		// banned for "too many corrupt pieces" first
		,
		[](test_disk& disk, int const i) {
			if (i == 2)
				disk = disk.send_corrupt_data(0, piece_index_t{0});
		}
		// a single piece with 4 blocks
		,
		4 * 0x4000,
		1,
		torrent_flags);

	TEST_CHECK(good_peer_contributed);
	TEST_CHECK(bad_peer_contributed);
	TEST_CHECK(banned_bad_peer);
	TEST_CHECK(!banned_good_peer);

	// for a v2 or hybrid torrent, the ban must have gone through
	// smart_ban's v2 attribution, not the v1 mechanism (which also runs
	// for v2 torrents but isn't what this test covers)
	if (!(torrent_flags & lt::create_torrent::v1_only))
		TEST_CHECK(bad_peer_banned_via_v2);
}

} // anonymous namespace

TORRENT_TEST(smart_ban_bans_correct_peer) { run_bans_correct_peer(create_torrent::v1_only); }

TORRENT_TEST(v2_hash_failure_bans_correct_peer) { run_bans_correct_peer(create_torrent::v2_only); }

TORRENT_TEST(hybrid_hash_failure_bans_correct_peer) { run_bans_correct_peer({}); }

// once a v2 torrent becomes a full seed, its piece picker is freed. If
// on-disk data is then silently corrupted and force_recheck() finds it,
// on_blocks_hashed() runs with no piece picker; found_on_disk is true for
// a recheck, so smart_ban::record_block_hashes() is skipped entirely (a
// check has no download to attribute a failure to), and this instead
// exercises on_blocks_hashed()'s own has_picker() guards on the failure
// path, which must not crash for lack of a picker.
TORRENT_TEST(smart_ban_survives_recheck_of_full_seed)
{
	// this torrent starts out as a full seed: real files on disk, with
	// correct content
	lt::add_torrent_params atp = ::create_torrent(0, true, 9, lt::create_torrent::v2_only);

	dsl_config network_cfg;
	sim::simulation sim{network_cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	lt::settings_pack pack = settings();
	pack.set_bool(settings_pack::enable_smart_ban, true);
	pack.set_int(settings_pack::checking_mem_usage, 1);

	auto ses = std::make_shared<lt::session>(pack, *ios);
	ses->async_add_torrent(atp);

	print_alerts(*ses);

	sim::timer t1(sim, lt::seconds(6), [&](boost::system::error_code const&) {
		lt::torrent_handle tor = ses->get_torrents()[0];
		// the initial check must have completed and turned this into a
		// seed (freeing the piece picker) for the scenario below to apply
		TEST_CHECK(tor.status().is_seeding);

		// corrupt piece 0 on disk, without changing the file size
		std::string const filename =
			lt::combine_path(atp.save_path, atp.ti->layout().file_path(lt::file_index_t{0}));
		std::ofstream f(filename, std::ios::binary | std::ios::in | std::ios::out);
		f.write("0000", 4);

		tor.force_recheck();
	});

	sim::timer t2(sim, lt::seconds(12), [&](boost::system::error_code const&) {
		lt::torrent_handle tor = ses->get_torrents()[0];
		lt::torrent_status st = tor.status(lt::torrent_handle::query_pieces);

		// the corruption must have been detected: only piece 0 is missing
		TEST_EQUAL(st.is_finished, false);
		lt::typed_bitfield<lt::piece_index_t> expected_pieces(st.pieces.size(), true);
		expected_pieces.clear_bit(piece_index_t{0});
		for (lt::piece_index_t p : expected_pieces.range())
			TEST_EQUAL(st.pieces[p], expected_pieces[p]);

		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}
