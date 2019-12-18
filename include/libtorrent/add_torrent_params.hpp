/*

Copyright (c) 2009-2018, Arvid Norberg
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
#include <ctime>

#include "libtorrent/storage_defs.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include "libtorrent/bitfield.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/download_priority.hpp"
#include "libtorrent/aux_/noexcept_movable.hpp"

namespace libtorrent {

	class torrent_info;
	struct torrent_plugin;
	struct torrent_handle;

TORRENT_VERSION_NAMESPACE_2

	// The add_torrent_params is a parameter pack for adding torrents to a
	// session. The key fields when adding a torrent are:
	//
	// * ti - when you have loaded a .torrent file into a torrent_info object
	// * info_hash - when you don't have the metadata (.torrent file) but. This
	//   is set when adding a magnet link.
	//
	// one of those fields must be set. Another mandatory field is
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
	// The ``add_torrent_params`` is also used when requesting resume data for a
	// torrent. It can be saved to and restored from a file and added back to a
	// new session. For serialization and de-serialization of
	// ``add_torrent_params`` objects, see read_resume_data() and
	// write_resume_data().
#include "libtorrent/aux_/disable_warnings_push.hpp"
	struct TORRENT_EXPORT add_torrent_params
	{
		// The constructor can be used to initialize the storage constructor,
		// which determines the storage mechanism for the downloaded or seeding
		// data for the torrent. For more information, see the ``storage`` field.
		explicit add_torrent_params(storage_constructor_type sc = default_storage_constructor);
		add_torrent_params(add_torrent_params&&) noexcept;
		add_torrent_params& operator=(add_torrent_params&&) = default;
		add_torrent_params(add_torrent_params const&);
		add_torrent_params& operator=(add_torrent_params const&);

		// These are all deprecated. use torrent_flags_t instead (in
		// libtorrent/torrent_flags.hpp)
#if TORRENT_ABI_VERSION == 1

		using flags_t = torrent_flags_t;

#define DECL_FLAG(name) \
		static constexpr torrent_flags_t TORRENT_DEPRECATED_MEMBER flag_##name = torrent_flags::name

			DECL_FLAG(seed_mode);
			DECL_FLAG(upload_mode);
			DECL_FLAG(share_mode);
			DECL_FLAG(apply_ip_filter);
			DECL_FLAG(paused);
			DECL_FLAG(auto_managed);
			DECL_FLAG(duplicate_is_error);
			DECL_FLAG(update_subscribe);
			DECL_FLAG(super_seeding);
			DECL_FLAG(sequential_download);
			DECL_FLAG(pinned);
			DECL_FLAG(stop_when_ready);
			DECL_FLAG(override_trackers);
			DECL_FLAG(override_web_seeds);
			DECL_FLAG(need_save_resume);
			DECL_FLAG(override_resume_data);
			DECL_FLAG(merge_resume_trackers);
			DECL_FLAG(use_resume_save_path);
			DECL_FLAG(merge_resume_http_seeds);
			DECL_FLAG(default_flags);
#undef DECL_FLAG
#endif // TORRENT_ABI_VERSION

#include "libtorrent/aux_/disable_warnings_pop.hpp"

		// filled in by the constructor and should be left untouched. It is used
		// for forward binary compatibility.
		int version = LIBTORRENT_VERSION_NUM;

		// torrent_info object with the torrent to add. Unless the
		// info_hash is set, this is required to be initialized.
		std::shared_ptr<torrent_info> ti;

		// If the torrent doesn't have a tracker, but relies on the DHT to find
		// peers, the ``trackers`` can specify tracker URLs for the torrent.
		aux::noexcept_movable<std::vector<std::string>> trackers;

		// the tiers the URLs in ``trackers`` belong to. Trackers belonging to
		// different tiers may be treated differently, as defined by the multi
		// tracker extension. This is optional, if not specified trackers are
		// assumed to be part of tier 0, or whichever the last tier was as
		// iterating over the trackers.
		aux::noexcept_movable<std::vector<int>> tracker_tiers;

		// a list of hostname and port pairs, representing DHT nodes to be added
		// to the session (if DHT is enabled). The hostname may be an IP address.
		aux::noexcept_movable<std::vector<std::pair<std::string, int>>> dht_nodes;

		// in case there's no other name in this torrent, this name will be used.
		// The name out of the torrent_info object takes precedence if available.
		std::string name;

		// the path where the torrent is or will be stored.
		//
		// .. note::
		// 	On windows this path (and other paths) are interpreted as UNC
		// 	paths. This means they must use backslashes as directory separators
		// 	and may not contain the special directories "." or "..".
		//
		// Setting this to an absolute path performs slightly better than a
		// relative path.
		std::string save_path;

		// One of the values from storage_mode_t. For more information, see
		// storage-allocation_.
		storage_mode_t storage_mode = storage_mode_sparse;

		// can be used to customize how the data is stored. The default storage
		// will simply write the data to the files it belongs to, but it could be
		// overridden to save everything to a single file at a specific location
		// or encrypt the content on disk for instance. For more information
		// about the storage_interface that needs to be implemented for a custom
		// storage, see storage_interface.
		aux::noexcept_movable<storage_constructor_type> storage;

		// The ``userdata`` parameter is optional and will be passed on to the
		// extension constructor functions, if any
		// (see torrent_handle::add_extension()).
		void* userdata = nullptr;

		// can be set to control the initial file priorities when adding a
		// torrent. The semantics are the same as for
		// ``torrent_handle::prioritize_files()``. The file priorities specified
		// in here take precedence over those specified in the resume data, if
		// any.
		aux::noexcept_movable<std::vector<download_priority_t>> file_priorities;

		// torrent extension construction functions can be added to this vector
		// to have them be added immediately when the torrent is constructed.
		// This may be desired over the torrent_handle::add_extension() in order
		// to avoid race conditions. For instance it may be important to have the
		// plugin catch events that happen very early on after the torrent is
		// created.
		aux::noexcept_movable<std::vector<std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)>>>
			extensions;

		// the default tracker id to be used when announcing to trackers. By
		// default this is empty, and no tracker ID is used, since this is an
		// optional argument. If a tracker returns a tracker ID, that ID is used
		// instead of this.
		std::string trackerid;

		// flags controlling aspects of this torrent and how it's added. See
		// torrent_flags_t for details.
		//
		// .. note::
		// 	The ``flags`` field is initialized with default flags by the
		// 	constructor. In order to preserve default behavior when clearing or
		// 	setting other flags, make sure to bitwise OR or in a flag or bitwise
		// 	AND the inverse of a flag to clear it.
		torrent_flags_t flags = torrent_flags::default_flags;

		// set this to the info hash of the torrent to add in case the info-hash
		// is the only known property of the torrent. i.e. you don't have a
		// .torrent file nor a magnet link.
		// To add a magnet link, use parse_magnet_uri() to populate fields in the
		// add_torrent_params object.
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
		//
		// For fine grained control over rate limits, including making them apply
		// to local peers, see peer-classes_.
		int max_uploads = -1;
		int max_connections = -1;

		// the upload and download rate limits for this torrent, specified in
		// bytes per second. -1 means unlimited.
		int upload_limit = -1;
		int download_limit = -1;

		// the total number of bytes uploaded and downloaded by this torrent so
		// far.
		std::int64_t total_uploaded = 0;
		std::int64_t total_downloaded = 0;

		// the number of seconds this torrent has spent in started, finished and
		// seeding state so far, respectively.
		int active_time = 0;
		int finished_time = 0;
		int seeding_time = 0;

		// if set to a non-zero value, this is the posix time of when this torrent
		// was first added, including previous runs/sessions. If set to zero, the
		// internal added_time will be set to the time of when add_torrent() is
		// called.
		std::time_t added_time = 0;
		std::time_t completed_time = 0;

		// if set to non-zero, initializes the time (expressed in posix time) when
		// we last saw a seed or peers that together formed a complete copy of the
		// torrent. If left set to zero, the internal counterpart to this field
		// will be updated when we see a seed or a distributed copies >= 1.0.
		std::time_t last_seen_complete = 0;

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
		int num_complete = -1;
		int num_incomplete = -1;
		int num_downloaded = -1;

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
		aux::noexcept_movable<std::vector<std::string>> http_seeds;
		aux::noexcept_movable<std::vector<std::string>> url_seeds;

		// peers to add to the torrent, to be tried to be connected to as
		// bittorrent peers.
		aux::noexcept_movable<std::vector<tcp::endpoint>> peers;

		// peers banned from this torrent. The will not be connected to
		aux::noexcept_movable<std::vector<tcp::endpoint>> banned_peers;

		// this is a map of partially downloaded piece. The key is the piece index
		// and the value is a bitfield where each bit represents a 16 kiB block.
		// A set bit means we have that block.
		aux::noexcept_movable<std::map<piece_index_t, bitfield>> unfinished_pieces;

		// this is a bitfield indicating which pieces we already have of this
		// torrent.
		typed_bitfield<piece_index_t> have_pieces;

		// when in seed_mode, pieces with a set bit in this bitfield have been
		// verified to be valid. Other pieces will be verified the first time a
		// peer requests it.
		typed_bitfield<piece_index_t> verified_pieces;

		// this sets the priorities for each individual piece in the torrent. Each
		// element in the vector represent the piece with the same index. If you
		// set both file- and piece priorities, file priorities will take
		// precedence.
		aux::noexcept_movable<std::vector<download_priority_t>> piece_priorities;

		// if this is a merkle tree torrent, and you're seeding, this field must
		// be set. It is all the hashes in the binary tree, with the root as the
		// first entry. See torrent_info::set_merkle_tree() for more info.
		aux::noexcept_movable<std::vector<sha1_hash>> merkle_tree;

		// this is a map of file indices in the torrent and new filenames to be
		// applied before the torrent is added.
		aux::noexcept_movable<std::map<file_index_t, std::string>> renamed_files;

		// the posix time of the last time payload was received or sent for this
		// torrent, respectively.
		std::time_t last_download = 0;
		std::time_t last_upload = 0;

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2

		// ``url`` can be set to a magnet link, in order to download the .torrent
		// file (also known as the metadata), specifically the info-dictionary,
		// from the bittorrent swarm. This may require access to the DHT, in case
		// the magnet link does not come with trackers.
		//
		// In earlier versions of libtorrent, the URL could be an HTTP or file://
		// url. These uses are deprecated and discouraged. When adding a torrent
		// by magnet link, it will be set to the ``downloading_metadata`` state
		// until the .torrent file has been downloaded. If there is any error
		// while downloading, the torrent will be stopped and the torrent error
		// state (``torrent_status::error``) will indicate what went wrong.
		std::string TORRENT_DEPRECATED_MEMBER url;

		// if ``uuid`` is specified, it is used to find duplicates. If another
		// torrent is already running with the same UUID as the one being added,
		// it will be considered a duplicate. This is mainly useful for RSS feed
		// items which has UUIDs specified.
		std::string TORRENT_DEPRECATED_MEMBER uuid;

		// The optional parameter, ``resume_data`` can be given if up to date
		// fast-resume data is available. The fast-resume data can be acquired
		// from a running torrent by calling save_resume_data() on
		// torrent_handle. See fast-resume_. The ``vector`` that is passed in
		// will be swapped into the running torrent instance with
		// ``std::vector::swap()``.
		aux::noexcept_movable<std::vector<char>> TORRENT_DEPRECATED_MEMBER resume_data;

		// to support the deprecated use case of reading the resume data into
		// resume_data field and getting a reject alert, any parse failure is
		// communicated forward into libtorrent via this field. If this is set, a
		// fastresume_rejected_alert will be posted.
		error_code internal_resume_data_error;
#endif // TORRENT_ABI_VERSION

	};

TORRENT_VERSION_NAMESPACE_2_END
}

#endif
