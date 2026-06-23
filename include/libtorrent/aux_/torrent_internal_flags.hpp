/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_AUX_TORRENT_INTERNAL_FLAGS_HPP_INCLUDED
#define TORRENT_AUX_TORRENT_INTERNAL_FLAGS_HPP_INCLUDED

#include "libtorrent/torrent_flags.hpp"

// engine-internal flag bits, sharing the torrent_flags_t bitfield with the
// public bits in libtorrent::torrent_flags. They are not user-settable and
// must be masked off (via torrent_flags::public_flags) before the torrent
// state is exposed to the user (torrent_handle::flags, torrent_status, resume
// data) or read back from a user-supplied value (add_torrent_params::flags,
// torrent_handle::set_flags).
//
// Bit positions start at 32 to leave room for future public bits to grow up
// from 25 without colliding.
namespace libtorrent::aux::torrent_internal_flags {

	// engine-driven runtime state, distinct from user-set torrent_flags
	// counterparts.
	constexpr lt::torrent_flags_t auto_sequential = 32_bit;
	constexpr lt::torrent_flags_t graceful_pause = 33_bit;

	// session-wide pause; torrent::is_paused() is the OR of the public paused
	// bit and this.
	constexpr lt::torrent_flags_t session_paused = 34_bit;

	// lifecycle / state-machine bits that have no public counterpart.
	constexpr lt::torrent_flags_t connections_initialized = 35_bit;
	constexpr lt::torrent_flags_t torrent_aborted = 36_bit;
	constexpr lt::torrent_flags_t have_all = 37_bit;
	constexpr lt::torrent_flags_t announce_to_trackers = 38_bit;
	constexpr lt::torrent_flags_t announce_to_lsd = 39_bit;
	constexpr lt::torrent_flags_t announce_to_dht = 40_bit;
	constexpr lt::torrent_flags_t has_incoming = 41_bit;
	constexpr lt::torrent_flags_t files_checked = 42_bit;
	constexpr lt::torrent_flags_t announcing = 43_bit;
	constexpr lt::torrent_flags_t added = 44_bit;
	constexpr lt::torrent_flags_t pending_active_change = 45_bit;
#if TORRENT_ABI_VERSION < 4
	constexpr lt::torrent_flags_t v2_piece_layers_validated = 46_bit;
#endif
	constexpr lt::torrent_flags_t ssl_torrent = 47_bit;
	constexpr lt::torrent_flags_t deleted = 48_bit;
	constexpr lt::torrent_flags_t moving_storage = 49_bit;
	constexpr lt::torrent_flags_t inactive = 50_bit;
	constexpr lt::torrent_flags_t torrent_initialized = 51_bit;
	constexpr lt::torrent_flags_t outstanding_file_priority = 52_bit;
	constexpr lt::torrent_flags_t complete_sent = 53_bit;
#if TORRENT_USE_ASSERTS
	constexpr lt::torrent_flags_t was_started = 54_bit;
	constexpr lt::torrent_flags_t outstanding_check_files = 55_bit;
#endif

	// peer-side helpers combining the public bit with the internal bit so
	// "is the torrent operating in this mode right now?" is a single
	// flag test.
	constexpr lt::torrent_flags_t effective_sequential =
		lt::torrent_flags::sequential_download | auto_sequential;
	constexpr lt::torrent_flags_t effective_upload_mode =
		lt::torrent_flags::upload_mode | graceful_pause;
	// "kind of paused": user-paused OR finishing a graceful pause. distinct
	// from is_paused() which also folds in session_paused.
	constexpr lt::torrent_flags_t effective_pause = lt::torrent_flags::paused | graceful_pause;
}

#endif
