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
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"

#include <string_view>

using namespace lt;

namespace {

// node 0 downloads a single, 4-block piece from node 1 (a normal seed) and
// node 2 (a seed that only ever sends corrupt data). A single piece with a
// tiny outstanding-request limit forces both peers to contribute blocks to
// it, ruling out the affinity for requesting a whole piece from one peer.
// v1 torrents rely on smart_ban's read-back comparison to attribute the bad
// block to the right peer; v2 (and hybrid) torrents identify it directly via
// merkle block hashes and torrent::penalize_peers() bans it outright, see
// torrent::piece_failed(). Either way, only the corrupt peer should be
// banned.
void run_bans_correct_peer(lt::create_flags_t const torrent_flags)
{
	bool banned_bad_peer = false;
	bool banned_good_peer = false;

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
				banned_bad_peer = true;
			if (msg.find("50.0.0.2") != std::string_view::npos)
				banned_good_peer = true;
		}
		// terminate
		,
		[](int const ticks, lt::session&) -> bool { return ticks > 30; }
		// customize_disk: node 2 always sends corrupt data
		,
		[](test_disk& disk, int const i) {
			if (i == 2)
				disk = disk.send_corrupt_data(0);
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
}

} // anonymous namespace

TORRENT_TEST(smart_ban_bans_correct_peer) { run_bans_correct_peer(create_torrent::v1_only); }

// disabled: pick_hashes() computes an out-of-range proof_layers count for a
// single-piece file (src/hash_picker.cpp, the m_piece_block_requests branch),
// tripping the validate_hash_request() assert in write_hash_request() as
// soon as any peer receives a block. Needs a fix in hash_picker before these
// can be re-enabled.

// TORRENT_TEST(v2_hash_failure_bans_correct_peer) { run_bans_correct_peer(create_torrent::v2_only); }

// TORRENT_TEST(hybrid_hash_failure_bans_correct_peer) { run_bans_correct_peer({}); }
