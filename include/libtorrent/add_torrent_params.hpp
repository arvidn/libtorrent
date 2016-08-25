/*

Copyright (c) 2009-2016, Arvid Norberg
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

#ifndef TORRENT_ADD_TORRENT_PARAMS_HPP_INCLUDED
#define TORRENT_ADD_TORRENT_PARAMS_HPP_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "libtorrent/storage_defs.hpp"
#include "libtorrent/peer_id.hpp" // sha1_hash
#include "libtorrent/version.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include "libtorrent/bitfield.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent
{
	class torrent_info;
	struct torrent_plugin;
	struct torrent_handle;

	// The add_torrent_params is a parameter pack for adding torrents to a
	// session. The key fields when adding a torrent are:
	//
	// * ti - when you have a .torrent file
	// * url - when you have a magnet link
	// * info_hash - when all you have is an info-hash (this is similar to a
	//   magnet link)
	//
	// one of those fields need to be set. Another mandatory field is
	// ``save_path``. The add_torrent_params object is passed into one of the
	// ``session::add_torrent()`` overloads or ``session::async_add_torrent()``.
	//
	// If you only specify the info-hash, the torrent file will be downloaded
	// from peers, which requires them to support the metadata extension. For
	// the metadata extension to work, libtorrent must be built with extensions
	// enabled (``TORRENT_DISABLE_EXTENSIONS`` must not be defined). It also
	// takes an optional ``name`` argument. This may be left empty in case no
	// name should be assigned to the torrent. In case it's not, the name is
	// used for the torrent as long as it doesn't have metadata. See
	// ``torrent_handle::name``.
	//
	struct TORRENT_EXPORT add_torrent_params
	{
		// The constructor can be used to initialize the storage constructor,
		// which determines the storage mechanism for the downloaded or seeding
		// data for the torrent. For more information, see the ``storage`` field.
		add_torrent_params(storage_constructor_type sc = default_storage_constructor)
			: version(LIBTORRENT_VERSION_NUM)
			, storage_mode(storage_mode_sparse)
			, storage(sc)
			, userdata(0)
			, flags(default_flags)
			, max_uploads(-1)
			, max_connections(-1)
			, upload_limit(-1)
			, download_limit(-1)
			, total_uploaded(0)
			, total_downloaded(0)
			, active_time(0)
			, finished_time(0)
			, seeding_time(0)
			, added_time(0)
			, completed_time(0)
			, last_seen_complete(0)
			, num_complete(-1)
			, num_incomplete(-1)
			, num_downloaded(-1)
		{}

		// values for the ``flags`` field
		enum flags_t : std::uint64_t
		{
			// If ``flag_seed_mode`` is set, libtorrent will assume that all files
			// are present for this torrent and that they all match the hashes in
			// the torrent file. Each time a peer requests to download a block,
			// the piece is verified against the hash, unless it has been verified
			// already. If a hash fails, the torrent will automatically leave the
			// seed mode and recheck all the files. The use case for this mode is
			// if a torrent is created and seeded, or if the user already know
			// that the files are complete, this is a way to avoid the initial
			// file checks, and significantly reduce the startup time.
			// 
			// Setting ``flag_seed_mode`` on a torrent without metadata (a
			// .torrent file) is a no-op and will be ignored.
			// 
			// If resume data is passed in with this torrent, the seed mode saved
			// in there will override the seed mode you set here.
			flag_seed_mode = 0x001,

			// If ``flag_upload_mode`` is set, the torrent will be initialized in
			// upload-mode, which means it will not make any piece requests. This
			// state is typically entered on disk I/O errors, and if the torrent
			// is also auto managed, it will be taken out of this state
			// periodically. This mode can be used to avoid race conditions when
			// adjusting priorities of pieces before allowing the torrent to start
			// downloading.
			// 
			// If the torrent is auto-managed (``flag_auto_managed``), the torrent
			// will eventually be taken out of upload-mode, regardless of how it
			// got there. If it's important to manually control when the torrent
			// leaves upload mode, don't make it auto managed.
			flag_upload_mode = 0x004,

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
			flag_share_mode = 0x008,

			// determines if the IP filter should apply to this torrent or not. By
			// default all torrents are subject to filtering by the IP filter
			// (i.e. this flag is set by default). This is useful if certain
			// torrents needs to be exempt for some reason, being an auto-update
			// torrent for instance.
			flag_apply_ip_filter = 0x010,

			// specifies whether or not the torrent is to be started in a paused
			// state. I.e. it won't connect to the tracker or any of the peers
			// until it's resumed. This is typically a good way of avoiding race
			// conditions when setting configuration options on torrents before
			// starting them.
			flag_paused = 0x020,

			// If the torrent is auto-managed (``flag_auto_managed``), the torrent
			// may be resumed at any point, regardless of how it paused. If it's
			// important to manually control when the torrent is paused and
			// resumed, don't make it auto managed.
			// 
			// If ``flag_auto_managed`` is set, the torrent will be queued,
			// started and seeded automatically by libtorrent. When this is set,
			// the torrent should also be started as paused. The default queue
			// order is the order the torrents were added. They are all downloaded
			// in that order. For more details, see queuing_.
			// 
			// If you pass in resume data, the auto_managed state of the torrent
			// when the resume data was saved will override the auto_managed state
			// you pass in here. You can override this by setting
			// ``override_resume_data``.
			flag_auto_managed = 0x040,
			flag_duplicate_is_error = 0x080,

			// on by default and means that this torrent will be part of state
			// updates when calling post_torrent_updates().
			flag_update_subscribe = 0x200,

			// sets the torrent into super seeding mode. If the torrent is not a
			// seed, this flag has no effect. It has the same effect as calling
			// ``torrent_handle::super_seeding(true)`` on the torrent handle
			// immediately after adding it.
			flag_super_seeding = 0x400,

			// sets the sequential download state for the torrent. It has the same
			// effect as calling ``torrent_handle::sequential_download(true)`` on
			// the torrent handle immediately after adding it.
			flag_sequential_download = 0x800,

			// indicates that this torrent should never be unloaded from RAM, even
			// if unloading torrents are allowed in general. Setting this makes
			// the torrent exempt from loading/unloading management.
			flag_pinned = 0x1000,

			// the stop when ready flag. Setting this flag is equivalent to calling
			// torrent_handle::stop_when_ready() immediately after the torrent is
			// added.
			flag_stop_when_ready = 0x2000,

			// when this flag is set, the tracker list in the add_torrent_params
			// object override any trackers from the torrent file. If the flag is
			// not set, the trackers from the add_torrent_params object will be
			// added to the list of trackers used by the torrent.
			flag_override_trackers = 0x4000,

			// If this flag is set, the web seeds from the add_torrent_params
			// object will override any web seeds in the torrent file. If it's not
			// set, web seeds in the add_torrent_params object will be added to the
			// list of web seeds used by the torrent.
			flag_override_web_seeds = 0x8000,

			// if this flag is set (which it is by default) the torrent will be
			// considered needing to save its resume data immediately as it's
			// added. New torrents that don't have any resume data should do that.
			// This flag is cleared by a successful call to read_resume_data()
			flag_need_save_resume = 0x10000,

#ifndef TORRENT_NO_DEPRECATE
			// If ``flag_override_resume_data`` is set, flags set for this torrent
			// in this ``add_torrent_params`` object will take precedence over
			// whatever states are saved in the resume data. For instance, the
			// ``paused``, ``auto_managed``, ``sequential_download``, ``seed_mode``,
			// ``super_seeding``, ``max_uploads``, ``max_connections``,
			// ``upload_limit`` and ``download_limit`` are all affected by this
			// flag. The intention of this flag is to have any field in
			// add_torrent_params configuring the torrent override the corresponding
			// configuration from the resume file, with the one exception of save
			// resume data, which has its own flag (for historic reasons).
			// If this flag is set, but file_priorities is empty, file priorities
			// are still loaded from the resume data, if present.
			flag_override_resume_data = 0x20000,

			// defaults to on and specifies whether tracker URLs loaded from
			// resume data should be added to the trackers in the torrent or
			// replace the trackers. When replacing trackers (i.e. this flag is not
			// set), any trackers passed in via add_torrent_params are also
			// replaced by any trackers in the resume data. The default behavior is
			// to have the resume data override the .torrent file _and_ the
			// trackers added in add_torrent_params.
			flag_merge_resume_trackers = 0x40000,

			// if this flag is set, the save path from the resume data file, if
			// present, is honored. This defaults to not being set, in which
			// case the save_path specified in add_torrent_params is always used.
			flag_use_resume_save_path = 0x80000,

			// defaults to on and specifies whether web seed URLs loaded from
			// resume data should be added to the ones in the torrent file or
			// replace them. No distinction is made between the two different kinds
			// of web seeds (`BEP 17`_ and `BEP 19`_). When replacing web seeds
			// (i.e. when this flag is not set), any web seeds passed in via
			// add_torrent_params are also replaced. The default behavior is to
			// have any web seeds in the resume data take precedence over whatever
			// is passed in here as well as the .torrent file.
			flag_merge_resume_http_seeds = 0x100000,
#endif

			// internal
			default_flags = flag_pinned | flag_update_subscribe
				| flag_auto_managed | flag_paused | flag_apply_ip_filter
				| flag_need_save_resume
#ifndef TORRENT_NO_DEPRECATE
				| flag_merge_resume_http_seeds
				| flag_merge_resume_trackers
#endif
		};

		// filled in by the constructor and should be left untouched. It is used
		// for forward binary compatibility.
		int version;

		// torrent_info object with the torrent to add. Unless the url or
		// info_hash is set, this is required to be initialized.
		std::shared_ptr<torrent_info> ti;

		// If the torrent doesn't have a tracker, but relies on the DHT to find
		// peers, the ``trackers`` can specify tracker URLs for the torrent.
		std::vector<std::string> trackers;

		// the tiers the URLs in ``trackers`` belong to. Trackers belonging to
		// different tiers may be treated differently, as defined by the multi
		// tracker extension. This is optional, if not specified trackers are
		// assumed to be part of tier 0, or whichever the last tier was as
		// iterating over the trackers.
		std::vector<int> tracker_tiers;

		// a list of hostname and port pairs, representing DHT nodes to be added
		// to the session (if DHT is enabled). The hostname may be an IP address.
		std::vector<std::pair<std::string, int>> dht_nodes;

		std::string name;

		// the path where the torrent is or will be stored.
		// 
		// .. note::
		// 	On windows this path (and other paths) are interpreted as UNC
		// 	paths. This means they must use backslashes as directory separators
		// 	and may not contain the special directories "." or "..".
		std::string save_path;

		// One of the values from storage_mode_t. For more information, see
		// storage-allocation_.
		storage_mode_t storage_mode;

		// can be used to customize how the data is stored. The default storage
		// will simply write the data to the files it belongs to, but it could be
		// overridden to save everything to a single file at a specific location
		// or encrypt the content on disk for instance. For more information
		// about the storage_interface that needs to be implemented for a custom
		// storage, see storage_interface.
		storage_constructor_type storage;

		// The ``userdata`` parameter is optional and will be passed on to the
		// extension constructor functions, if any
		// (see torrent_handle::add_extension()).
		void* userdata;

		// can be set to control the initial file priorities when adding a
		// torrent. The semantics are the same as for
		// ``torrent_handle::prioritize_files()``.
		std::vector<std::uint8_t> file_priorities;

		// torrent extension construction functions can be added to this vector
		// to have them be added immediately when the torrent is constructed.
		// This may be desired over the torrent_handle::add_extension() in order
		// to avoid race conditions. For instance it may be important to have the
		// plugin catch events that happen very early on after the torrent is
		// created.
		std::vector<std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)>>
			extensions;

		// the default tracker id to be used when announcing to trackers. By
		// default this is empty, and no tracker ID is used, since this is an
		// optional argument. If a tracker returns a tracker ID, that ID is used
		// instead of this.
		std::string trackerid;

		// If you specify a ``url``, the torrent will be set in
		// ``downloading_metadata`` state until the .torrent file has been
		// downloaded. If there's any error while downloading, the torrent will
		// be stopped and the torrent error state (``torrent_status::error``)
		// will indicate what went wrong. The ``url`` may be set to a magnet link.
		std::string url;

		// flags controlling aspects of this torrent and how it's added. See
		// flags_t for details.
		// 
		// .. note::
		// 	The ``flags`` field is initialized with default flags by the
		// 	constructor. In order to preserve default behavior when clearing or
		// 	setting other flags, make sure to bitwise OR or in a flag or bitwise
		// 	AND the inverse of a flag to clear it.
		std::uint64_t flags;

		// set this to the info hash of the torrent to add in case the info-hash
		// is the only known property of the torrent. i.e. you don't have a
		// .torrent file nor a magnet link.
		sha1_hash info_hash;

		// ``max_uploads``, ``max_connections``, ``upload_limit``,
		// ``download_limit`` correspond to the ``set_max_uploads()``,
		// ``set_max_connections()``, ``set_upload_limit()`` and
		// ``set_download_limit()`` functions on torrent_handle. These values let
		// you initialize these settings when the torrent is added, instead of
		// calling these functions immediately following adding it.
		// 
		// -1 means unlimited on these settings just like their counterpart
		// functions on torrent_handle
		int max_uploads;
		int max_connections;
		int upload_limit;
		int download_limit;

		// the total number of bytes uploaded and downloaded by this torrent so
		// far.
		std::int64_t total_uploaded;
		std::int64_t total_downloaded;

		// the number of seconds this torrent has spent in started, finished and
		// seeding state so far, respectively.
		int active_time;
		int finished_time;
		int seeding_time;

		// if set to a non-zero value, this is the posix time of when this torrent
		// was first added, including previous runs/sessions. If set to zero, the
		// internal added_time will be set to the time of when add_torrent() is
		// called.
		time_t added_time;
		time_t completed_time;

		// if set to non-zero, initializes the time (expressed in posix time) when
		// we last saw a seed or peers that together formed a complete copy of the
		// torrent. If left set to zero, the internal counterpart to this field
		// will be updated when we see a seed or a distributed copies >= 1.0.
		time_t last_seen_complete;

		// these field can be used to initialize the torrent's cached scrape data.
		// The scrape data is high level metadata about the current state of the
		// swarm, as returned by the tracker (either when announcing to it or by
		// sending a specific scrape request). ``num_complete`` is the number of
		// peers in the swarm that are seeds, or have every piece in the torrent.
		// ``num_incomplete`` is the number of peers in the swarm that do not have
		// every piece. ``num_downloaded`` is the number of times the torrent has
		// been downloaded (not initiated, but the number of times a download has
		// completed).
		// 
		// Leaving any of these values set to -1 indicates we don't know, or we
		// have not received any scrape data.
		int num_complete;
		int num_incomplete;
		int num_downloaded;

		// URLs can be added to these two lists to specify additional web
		// seeds to be used by the torrent. If the ``flag_override_web_seeds``
		// is set, these will be the _only_ ones to be used. i.e. any web seeds
		// found in the .torrent file will be overridden.
		// 
		// http_seeds expects URLs to web servers implementing the original HTTP
		// seed specification `BEP 17`_.
		// 
		// url_seeds expects URLs to regular web servers, aka "get right" style,
		// specified in `BEP 19`_.
		std::vector<std::string> http_seeds;
		std::vector<std::string> url_seeds;

		// peers to add to the torrent, to be tried to be connected to as
		// bittorrent peers.
		std::vector<tcp::endpoint> peers;

		// peers banned from this torrent. The will not be connected to
		std::vector<tcp::endpoint> banned_peers;

		// this is a map of partially downloaded piece. The key is the piece index
		// and the value is a bitfield where each bit represents a 16 kiB block.
		// A set bit means we have that block.
		std::map<int, bitfield> unfinished_pieces;

		// this is a bitfield indicating which pieces we already have of this
		// torrent.
		bitfield have_pieces;

		// when in seed_mode, pieces with a set bit in this bitfield have been
		// verified to be valid. Other pieces will be verified the first time a
		// peer requests it.
		bitfield verified_pieces;

		// this sets the priorities for each individual piece in the torrent. Each
		// element in the vector represent the piece with the same index. If you
		// set both file- and piece priorities, file priorities will take
		// precedence.
		std::vector<std::uint8_t> piece_priorities;

		// if this is a merkle tree torrent, and you're seeding, this field must
		// be set. It is all the hashes in the binary tree, with the root as the
		// first entry. See torrent_info::set_merkle_tree() for more info.
		std::vector<sha1_hash> merkle_tree;

		// this is a map of file indices in the torrent and new filenames to be
		// applied before the torrent is added.
		std::map<int, std::string> renamed_files;

#ifndef TORRENT_NO_DEPRECATE
	// deprecated in 1.2
		// if ``uuid`` is specified, it is used to find duplicates. If another
		// torrent is already running with the same UUID as the one being added,
		// it will be considered a duplicate. This is mainly useful for RSS feed
		// items which has UUIDs specified.
		std::string uuid;

		// The optional parameter, ``resume_data`` can be given if up to date
		// fast-resume data is available. The fast-resume data can be acquired
		// from a running torrent by calling save_resume_data() on
		// torrent_handle. See fast-resume_. The ``vector`` that is passed in
		// will be swapped into the running torrent instance with
		// ``std::vector::swap()``.
		std::vector<char> resume_data;

		// to support the deprecated use case of reading the resume data into
		// resume_data field and getting a reject alert, any parse failure is
		// communicated forward into libtorrent via this field. If this is set, a
		// fastresume_rejected_alert will be posted.
		error_code internal_resume_data_error;
#else
		// hidden
		// to maintain ABI compatibility
		std::string deprecated1;
		std::string deprecated2;
		std::vector<char> deprecated3;
		error_code deprecated4;
#endif

	};
}

#endif
