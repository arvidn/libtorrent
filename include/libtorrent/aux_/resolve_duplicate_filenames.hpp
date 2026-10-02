/*

Copyright (c) 2003-2011, 2013-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RESOLVE_DUPLICATE_FILENAMES_HPP_INCLUDED
#define TORRENT_RESOLVE_DUPLICATE_FILENAMES_HPP_INCLUDED

#include <string>
#include <map>
#include "libtorrent/units.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/file_storage.hpp" // for path_index_t
#include "libtorrent/aux_/vector.hpp"

namespace libtorrent::aux {
std::map<file_index_t, std::string> resolve_duplicate_filenames(
	file_storage const& fs, load_torrent_limits const& cfg, error_code& ec);

// internal
// does the actual duplicate-resolution work, given per-path_element
// crc32 hashes already computed by resolve_duplicate_filenames() (see
// file_storage::compute_element_hashes()). Exposed (only) for unit
// tests that need to feed it a crafted hash array to exercise its
// collision-scanning cost bound.
TORRENT_EXTRA_EXPORT std::map<file_index_t, std::string> resolve_duplicate_filenames_slow(
	file_storage const& fs,
	aux::vector<std::uint32_t, path_index_t> const& eh,
	int max_duplicate_filenames,
	bool case_sensitive,
	error_code& ec);

// internal
// deduplicates directory entries: renames whichever siblings (files,
// symlinks, or directories) collide under the same parent,
// case-insensitively if path_sanitize_flags::
// case_insensitive_deduplication is set. Returns each rename as a
// (path_index_t, new name) pair, or an empty map with ec set on error.
TORRENT_EXTRA_EXPORT std::map<path_index_t, std::string> resolve_directory_duplicates(
	file_storage const& fs, load_torrent_limits const& cfg, error_code& ec);
}

#endif
