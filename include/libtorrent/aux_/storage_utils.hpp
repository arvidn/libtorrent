/*

Copyright (c) 2017-2020, 2022, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STORAGE_UTILS_HPP_INCLUDE
#define TORRENT_STORAGE_UTILS_HPP_INCLUDE

#include <cstdint>
#include <string>
#include <functional>

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/disk_interface.hpp" // for status_t
#include "libtorrent/session_types.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent {

	// TODO: 3 remove this typedef, and use span<char const> for disk write
	// operations
	using iovec_t = span<char>;

namespace aux {

	struct stat_cache;

	// this is a read or write operation so that readwrite() knows
	// what to do when it's actually touching the file
	using fileop = std::function<int(file_index_t, std::int64_t, span<char>, storage_error&)>;

	// this function is responsible for turning read and write operations in the
	// torrent space (pieces) into read and write operations in the filesystem
	// space (files on disk).
	TORRENT_EXTRA_EXPORT int readwrite(file_storage const& files
		, span<char> buf, piece_index_t piece, int offset
		, storage_error& ec, fileop op);

	// moves the files in file_storage f from ``save_path`` to
	// ``destination_save_path`` according to the rules defined by ``flags``.
	// returns the status code and the new save_path.
	TORRENT_EXTRA_EXPORT std::pair<status_t, std::string>
	move_storage(file_storage const& f
		, std::string save_path
		, std::string const& destination_save_path
		, std::function<void(std::string const&, lt::error_code&)> const& move_partfile
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
		, stat_cache& cache
		, storage_error& ec);

	TORRENT_EXTRA_EXPORT int read_zeroes(span<char> bufs);

	TORRENT_EXTRA_EXPORT int hash_zeroes(hasher& ph, std::int64_t size);

	TORRENT_EXTRA_EXPORT void initialize_storage(
		file_storage const& fs
		, std::string const& save_path
		, stat_cache& sc
		, aux::vector<download_priority_t, file_index_t> const& file_priority
		, std::function<void(file_index_t, storage_error&)> create_file
		, std::function<void(std::string const&, std::string const&, storage_error&)> create_link
		, std::function<void(file_index_t, std::int64_t)> oversized_file
		, storage_error& ec);

	TORRENT_EXTRA_EXPORT void create_symlink(
		std::string const& target
		, std::string const& link
		, storage_error& ec);

	TORRENT_EXTRA_EXPORT void move_file(std::string const& f
		, std::string const& newf, storage_error& se);

	TORRENT_EXTRA_EXPORT void copy_file(std::string const& f
		, std::string const& newf, storage_error& se);
}}

#endif
