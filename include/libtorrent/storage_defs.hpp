/*

Copyright (c) 2023, Vladimir Golovnev
Copyright (c) 2006-2007, 2009, 2013-2014, 2016-2022, Arvid Norberg
Copyright (c) 2016, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STORAGE_DEFS_HPP_INCLUDE
#define TORRENT_STORAGE_DEFS_HPP_INCLUDE

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/download_priority.hpp"
#include <functional>
#include <string>

namespace libtorrent {

	using storage_index_t = aux::strong_typedef<std::uint32_t, struct storage_index_tag_t>;

	// types of storage allocation used for add_torrent_params::storage_mode.
	enum storage_mode_t
	{
		// All pieces will be written to their final position, all files will be
		// allocated in full when the torrent is first started. This mode minimizes
		// fragmentation but could be a costly operation.
		storage_mode_allocate,

		// All pieces will be written to the place where they belong and sparse files
		// will be used. This is the recommended, and default mode.
		storage_mode_sparse
	};

	// flags for async_move_storage
	enum class move_flags_t : std::uint8_t
	{
		// replace any files in the destination when copying
		// or moving the storage
		always_replace_files,

		// if any files that we want to copy exist in the destination
		// exist, fail the whole operation and don't perform
		// any copy or move. There is an inherent race condition
		// in this mode. The files are checked for existence before
		// the operation starts. In between the check and performing
		// the copy, the destination files may be created, in which
		// case they are replaced.
		fail_if_exist,

		// if any file exist in the target, take those files instead
		// of the ones we may have in the source.
		dont_replace,

		// don't move any source files, just forget about them
		// and begin checking files at new save path
		reset_save_path,

		// don't move any source files, just change save path
		// and continue working without any checks
		reset_save_path_unchecked
	};

#if TORRENT_ABI_VERSION == 1
	// deprecated in 1.2
	enum deprecated_move_flags_t
	{
		always_replace_files TORRENT_DEPRECATED_ENUM,
		fail_if_exist TORRENT_DEPRECATED_ENUM,
		dont_replace TORRENT_DEPRECATED_ENUM
	};
#endif

	// a parameter pack used to construct the storage for a torrent, used in
	// disk_interface
	struct TORRENT_EXPORT storage_params
	{
		storage_params(file_storage const& f, lt::renamed_files const& mf
			, std::string const& sp, storage_mode_t const sm
			, aux::vector<download_priority_t, file_index_t> const& prio
			, sha1_hash const& ih, bool v1_torrent, bool v2_torrent)
			: files(f)
			, renamed_files(mf)
			, path(sp)
			, mode(sm)
			, priorities(prio)
			, info_hash(ih)
			, v1(v1_torrent)
			, v2(v2_torrent)
		{}
		file_storage const& files;
		lt::renamed_files const& renamed_files;
		std::string const& path;
		storage_mode_t mode{storage_mode_sparse};
		aux::vector<download_priority_t, file_index_t> const& priorities;
		sha1_hash info_hash;
		bool v1;
		bool v2;
	};
}

#endif
