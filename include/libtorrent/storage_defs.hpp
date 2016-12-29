/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifndef TORRENT_STORAGE_DEFS_HPP_INCLUDE
#define TORRENT_STORAGE_DEFS_HPP_INCLUDE

#include "libtorrent/config.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/sha1_hash.hpp"
#include <functional>
#include <string>

namespace libtorrent {

	struct TORRENT_EXPORT storage_interface;
	class file_storage;
	struct file_pool;
	class torrent_info;

	struct storage_index_tag_t {};
	using storage_index_t = aux::strong_typedef<std::uint32_t, storage_index_tag_t>;

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

	enum class status_t : std::uint8_t
	{
		// return values from check_fastresume, and move_storage
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

#ifndef TORRENT_NO_DEPRECATE
	// deprecated in 1.2
	enum deprecated_move_flags_t
	{
		always_replace_files TORRENT_DEPRECATED_ENUM,
		fail_if_exist TORRENT_DEPRECATED_ENUM,
		dont_replace TORRENT_DEPRECATED_ENUM
	};
#endif

	struct TORRENT_EXPORT storage_params
	{
		storage_params(file_storage const& f, file_storage const* mf
			, std::string const& sp, storage_mode_t const sm
			, aux::vector<std::uint8_t, file_index_t> const& prio
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
		aux::vector<std::uint8_t, file_index_t> const& priorities;
		sha1_hash const& info_hash;
	};
}

#endif
