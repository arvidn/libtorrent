/*

Copyright (c) 2006-2007, 2009, 2013-2014, 2016-2019, Arvid Norberg
Copyright (c) 2016, Alden Torres
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
		// allocated in full when the torrent is first started. This is done with
		// ``fallocate()`` and similar calls. This mode minimizes fragmentation.
		storage_mode_allocate,

		// All pieces will be written to the place where they belong and sparse files
		// will be used. This is the recommended, and default mode.
		storage_mode_sparse
	};

	// return values from check_fastresume, and move_storage
	enum class status_t : std::uint8_t
	{
		no_error,
		fatal_disk_error,
		need_full_check,
		file_exist
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
		dont_replace
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

	struct TORRENT_EXTRA_EXPORT storage_params
	{
		storage_params(file_storage const& f, file_storage const* mf
			, std::string const& sp, storage_mode_t const sm
			, aux::vector<download_priority_t, file_index_t> const& prio
			, sha1_hash const& ih)
			: files(f)
			, mapped_files(mf)
			, path(sp)
			, mode(sm)
			, priorities(prio)
			, info_hash(ih)
		{}
		file_storage const& files;
		file_storage const* mapped_files = nullptr; // optional
		std::string const& path;
		storage_mode_t mode{storage_mode_sparse};
		aux::vector<download_priority_t, file_index_t> const& priorities;
		sha1_hash info_hash;
	};
}

#endif
