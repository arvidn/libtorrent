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

#ifndef TORRENT_STORAGE_UTILS_HPP_INCLUDE
#define TORRENT_STORAGE_UTILS_HPP_INCLUDE

#include <cstdint>
#include <string>

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/storage_defs.hpp" // for status_t
#include "libtorrent/session_types.hpp"

namespace libtorrent {

	struct part_file;
	struct stat_cache;

	// TODO: 3 remove this typedef, and use span<char const> for disk write
	// operations
	using iovec_t = span<char>;

namespace aux {

	TORRENT_EXTRA_EXPORT int copy_bufs(span<iovec_t const> bufs
		, int bytes, span<iovec_t> target);
	TORRENT_EXTRA_EXPORT span<iovec_t> advance_bufs(span<iovec_t> bufs, int bytes);
	TORRENT_EXTRA_EXPORT void clear_bufs(span<iovec_t const> bufs);

	// this is a read or write operation so that readwritev() knows
	// what to do when it's actually touching the file
	using fileop = std::function<int(file_index_t, std::int64_t, span<iovec_t const>, storage_error&)>;

	// this function is responsible for turning read and write operations in the
	// torrent space (pieces) into read and write operations in the filesystem
	// space (files on disk).
	TORRENT_EXTRA_EXPORT int readwritev(file_storage const& files
		, span<iovec_t const> bufs, piece_index_t piece, int offset
		, storage_error& ec, fileop op);

	// moves the files in file_storage f from ``save_path`` to
	// ``destination_save_path`` according to the rules defined by ``flags``.
	// returns the status code and the new save_path.
	TORRENT_EXTRA_EXPORT std::pair<status_t, std::string>
	move_storage(file_storage const& f
		, std::string const& save_path
		, std::string const& destination_save_path
		, part_file* pf
		, move_flags_t flags, storage_error& ec);

	// deletes the files on fs from save_path according to options. Options may
	// opt to only delete the partfile
	TORRENT_EXTRA_EXPORT void
	delete_files(file_storage const& fs, std::string const& save_path
		, std::string const& part_file_name, remove_flags_t options, storage_error& ec);

	TORRENT_EXTRA_EXPORT bool
	verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, file_storage const& fs
		, aux::vector<download_priority_t, file_index_t> const& file_priority
		, stat_cache& stat
		, std::string const& save_path
		, storage_error& ec);

	// given the save_path, stat all files on file_storage until one exists. If a
	// file exists, return true, otherwise return false.
	TORRENT_EXTRA_EXPORT bool has_any_file(
		file_storage const& fs
		, std::string const& save_path
		, stat_cache& stat
		, storage_error& ec);
}}

#endif
