/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef TORRENT_TORRENT_FLAGS_HPP
#define TORRENT_TORRENT_FLAGS_HPP

#include <cstdint>

#include "libtorrent/config.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

using torrent_flags_t = flags::bitfield_flag<std::uint64_t, struct torrent_flags_tag>;

namespace torrent_flags {

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_warnings_push.hpp"
#endif

	// If ``seed_mode`` is set, libtorrent will assume that all files
	// are present for this torrent and that they all match the hashes in
	// the torrent file. Each time a peer requests to download a block,
	// the piece is verified against the hash, unless it has been verified
	// already. If a hash fails, the torrent will automatically leave the
	// seed mode and recheck all the files. The use case for this mode is
	// if a torrent is created and seeded, or if the user already know
	// that the files are complete, this is a way to avoid the initial
	// file checks, and significantly reduce the startup time.
	//
	// Setting ``seed_mode`` on a torrent without metadata (a
	// .torrent file) is a no-op and will be ignored.
	//
	// It is not possible to *set* the ``seed_mode`` flag on a torrent after it has
	// been added to a session. It is possible to *clear* it though.
	constexpr torrent_flags_t seed_mode = 0_bit;

	// If ``upload_mode`` is set, the torrent will be initialized in
	// upload-mode, which means it will not make any piece requests. This
	// state is typically entered on disk I/O errors, and if the torrent
	// is also auto managed, it will be taken out of this state
	// periodically (see ``settings_pack::optimistic_disk_retry``).
	//
	// This mode can be used to avoid race conditions when
	// adjusting priorities of pieces before allowing the torrent to start
	// downloading.
	//
	// If the torrent is auto-managed (``auto_managed``), the torrent
	// will eventually be taken out of upload-mode, regardless of how it
	// got there. If it's important to manually control when the torrent
	// leaves upload mode, don't make it auto managed.
	constexpr torrent_flags_t upload_mode = 1_bit;

	// determines if the torrent should be added in *share mode* or not.
	// Share mode indicates that we are not interested in downloading the
	// torrent, but merely want to improve our share ratio (i.e. increase
	// it). A torrent started in share mode will do its best to never
	// download more than it uploads to the swarm. If the swarm does not
	// have enough demand for upload capacity, the torrent will not
	// download anything. This mode is intended to be safe to add any
	// number of torrents to, without manual screening, without the risk
	// of downloading more than is uploaded.
	//
	// A torrent in share mode sets the priority to all pieces to 0,
	// except for the pieces that are downloaded, when pieces are decided
	// to be downloaded. This affects the progress bar, which might be set
	// to "100% finished" most of the time. Do not change file or piece
	// priorities for torrents in share mode, it will make it not work.
	//
	// The share mode has one setting, the share ratio target, see
	// ``settings_pack::share_mode_target`` for more info.
	constexpr torrent_flags_t share_mode = 2_bit;

	// determines if the IP filter should apply to this torrent or not. By
	// default all torrents are subject to filtering by the IP filter
	// (i.e. this flag is set by default). This is useful if certain
	// torrents needs to be exempt for some reason, being an auto-update
	// torrent for instance.
	constexpr torrent_flags_t apply_ip_filter = 3_bit;

	// specifies whether or not the torrent is to be started in a paused
	// state. I.e. it won't connect to the tracker or any of the peers
	// until it's resumed. This is typically a good way of avoiding race
	// conditions when setting configuration options on torrents before
	// starting them.
	constexpr torrent_flags_t paused = 4_bit;

	// If the torrent is auto-managed (``auto_managed``), the torrent
	// may be resumed at any point, regardless of how it paused. If it's
	// important to manually control when the torrent is paused and
	// resumed, don't make it auto managed.
	//
	// If ``auto_managed`` is set, the torrent will be queued,
	// started and seeded automatically by libtorrent. When this is set,
	// the torrent should also be started as paused. The default queue
	// order is the order the torrents were added. They are all downloaded
	// in that order. For more details, see queuing_.
	constexpr torrent_flags_t auto_managed = 5_bit;

	// used in add_torrent_params to indicate that it's an error to attempt
	// to add a torrent that's already in the session. If it's not considered an
	// error, a handle to the existing torrent is returned.
	constexpr torrent_flags_t duplicate_is_error = 6_bit;

	// on by default and means that this torrent will be part of state
	// updates when calling post_torrent_updates().
	constexpr torrent_flags_t update_subscribe = 7_bit;

	// sets the torrent into super seeding/initial seeding mode. If the torrent
	// is not a seed, this flag has no effect.
	constexpr torrent_flags_t super_seeding = 8_bit;

	// sets the sequential download state for the torrent. In this mode the
	// piece picker will pick pieces with low index numbers before pieces with
	// high indices. The actual pieces that are picked depend on other factors
	// still, such as which pieces a peer has and whether it is in parole mode
	// or "prefer whole pieces"-mode. Sequential mode is not ideal for streaming
	// media. For that, see set_piece_deadline() instead.
	constexpr torrent_flags_t sequential_download = 9_bit;

	// When this flag is set, the torrent will *force stop* whenever it
	// transitions from a non-data-transferring state into a data-transferring
	// state (referred to as being ready to download or seed). This is useful
	// for torrents that should not start downloading or seeding yet, but want
	// to be made ready to do so. A torrent may need to have its files checked
	// for instance, so it needs to be started and possibly queued for checking
	// (auto-managed and started) but as soon as it's done, it should be
	// stopped.
	//
	// *Force stopped* means auto-managed is set to false and it's paused. As
	// if the auto_manages flag is cleared and the paused flag is set on the torrent.
	//
	// Note that the torrent may transition into a downloading state while
	// calling this function, and since the logic is edge triggered you may
	// miss the edge. To avoid this race, if the torrent already is in a
	// downloading state when this call is made, it will trigger the
	// stop-when-ready immediately.
	//
	// When the stop-when-ready logic fires, the flag is cleared. Any
	// subsequent transitions between downloading and non-downloading states
	// will not be affected, until this function is used to set it again.
	//
	// The behavior is more robust when setting this flag as part of adding
	// the torrent. See add_torrent_params.
	//
	// The stop-when-ready flag fixes the inherent race condition of waiting
	// for the state_changed_alert and then call pause(). The download/seeding
	// will most likely start in between posting the alert and receiving the
	// call to pause.
	//
	// A downloading state is one where peers are being connected. Which means
	// just downloading the metadata via the ``ut_metadata`` extension counts
	// as a downloading state. In order to stop a torrent once the metadata
	// has been downloaded, instead set all file priorities to dont_download
	constexpr torrent_flags_t stop_when_ready = 10_bit;

	// when this flag is set, the tracker list in the add_torrent_params
	// object override any trackers from the torrent file. If the flag is
	// not set, the trackers from the add_torrent_params object will be
	// added to the list of trackers used by the torrent.
	// This flag is set by read_resume_data() if there are trackers present in
	// the resume data file. This effectively makes the trackers saved in the
	// resume data take precedence over the original trackers. This includes if
	// there's an empty list of trackers, to support the case where they were
	// explicitly removed in the previous session.
	constexpr torrent_flags_t override_trackers = 11_bit;

	// If this flag is set, the web seeds from the add_torrent_params
	// object will override any web seeds in the torrent file. If it's not
	// set, web seeds in the add_torrent_params object will be added to the
	// list of web seeds used by the torrent.
	// This flag is set by read_resume_data() if there are web seeds present in
	// the resume data file. This effectively makes the web seeds saved in the
	// resume data take precedence over the original ones. This includes if
	// there's an empty list of web seeds, to support the case where they were
	// explicitly removed in the previous session.
	constexpr torrent_flags_t override_web_seeds = 12_bit;

	// if this flag is set (which it is by default) the torrent will be
	// considered needing to save its resume data immediately as it's
	// added. New torrents that don't have any resume data should do that.
	// This flag is cleared by a successful call to save_resume_data()
	constexpr torrent_flags_t need_save_resume = 13_bit;

#if TORRENT_ABI_VERSION == 1
	// indicates that this torrent should never be unloaded from RAM, even
	// if unloading torrents are allowed in general. Setting this makes
	// the torrent exempt from loading/unloading management.
	constexpr torrent_flags_t TORRENT_DEPRECATED_MEMBER pinned = 14_bit;

	// If ``override_resume_data`` is set, flags set for this torrent
	// in this ``add_torrent_params`` object will take precedence over
	// whatever states are saved in the resume data. For instance, the
	// ``paused``, ``auto_managed``, ``sequential_download``, ``seed_mode``,
	// ``super_seeding``, ``max_uploads``, ``max_connections``,
	// ``upload_limit`` and ``download_limit`` are all affected by this
	// flag. The intention of this flag is to have any field in
	// add_torrent_params configuring the torrent override the corresponding
	// configuration from the resume file, with the one exception of save
	// resume data, which has its own flag (for historic reasons).
	// "file_priorities" and "save_path" are not affected by this flag.
	constexpr torrent_flags_t TORRENT_DEPRECATED_MEMBER override_resume_data = 15_bit;

	// defaults to on and specifies whether tracker URLs loaded from
	// resume data should be added to the trackers in the torrent or
	// replace the trackers. When replacing trackers (i.e. this flag is not
	// set), any trackers passed in via add_torrent_params are also
	// replaced by any trackers in the resume data. The default behavior is
	// to have the resume data override the .torrent file _and_ the
	// trackers added in add_torrent_params.
	constexpr torrent_flags_t TORRENT_DEPRECATED_MEMBER merge_resume_trackers = 16_bit;

	// if this flag is set, the save path from the resume data file, if
	// present, is honored. This defaults to not being set, in which
	// case the save_path specified in add_torrent_params is always used.
	constexpr torrent_flags_t TORRENT_DEPRECATED_MEMBER use_resume_save_path = 17_bit;

	// defaults to on and specifies whether web seed URLs loaded from
	// resume data should be added to the ones in the torrent file or
	// replace them. No distinction is made between the two different kinds
	// of web seeds (`BEP 17`_ and `BEP 19`_). When replacing web seeds
	// (i.e. when this flag is not set), any web seeds passed in via
	// add_torrent_params are also replaced. The default behavior is to
	// have any web seeds in the resume data take precedence over whatever
	// is passed in here as well as the .torrent file.
	constexpr torrent_flags_t TORRENT_DEPRECATED_MEMBER merge_resume_http_seeds = 18_bit;
#endif

	// set this flag to disable DHT for this torrent. This lets you have the DHT
	// enabled for the whole client, and still have specific torrents not
	// participating in it. i.e. not announcing to the DHT nor picking up peers
	// from it.
	constexpr torrent_flags_t disable_dht = 19_bit;

	// set this flag to disable local service discovery for this torrent.
	constexpr torrent_flags_t disable_lsd = 20_bit;

	// set this flag to disable peer exchange for this torrent.
	constexpr torrent_flags_t disable_pex = 21_bit;

	// all torrent flags combined. Can conveniently be used when creating masks
	// for flags
	constexpr torrent_flags_t all = torrent_flags_t::all();

	// internal
	constexpr torrent_flags_t default_flags =
		torrent_flags::update_subscribe
		| torrent_flags::auto_managed
		| torrent_flags::paused
		| torrent_flags::apply_ip_filter
		| torrent_flags::need_save_resume
#if TORRENT_ABI_VERSION == 1
		| torrent_flags::pinned
		| torrent_flags::merge_resume_http_seeds
		| torrent_flags::merge_resume_trackers
#endif
		;

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

} // torrent_flags
} // libtorrent

#endif

