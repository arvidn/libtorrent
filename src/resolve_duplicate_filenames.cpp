/*

Copyright (c) 2003-2011, 2013-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <map>
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/string_view.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/string_util.hpp"
#include "libtorrent/aux_/resolve_duplicate_filenames.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/path.hpp"

namespace libtorrent::aux {

namespace {

	template <class CRC>
	void process_string_lowercase(CRC& crc, string_view str)
	{
		for (char const c : str)
			crc.process_byte(aux::to_lower(c) & 0xff);
	}

	struct name_entry
	{
		// file_index_t{-1} means this entry is a directory, identified by
		// `dir` rather than a real file index
		file_index_t idx;
		aux::path_index_t dir;
	};

	std::map<file_index_t, std::string> resolve_duplicate_filenames_slow(file_storage const& fs,
		file_storage::element_hashes const& eh,
		int const max_duplicate_filenames,
		error_code& ec)
	{
		// maps filename hash to either a file index or (if idx is negative) a
		// directory, to make sure no files are allowed to collide with them
		std::unordered_multimap<std::uint32_t, name_entry> files;

		std::map<file_index_t, std::string> ret;

		// two different directory path_elements can legitimately hash the
		// same here, so inserting them all into a multimap without
		// checking for an existing entry is correct, not just convenient.
		// The parser's dedup cache (cached_directory() in
		// torrent_info.cpp) keys a directory by its raw, unsanitized text,
		// while this hash is computed from the sanitized, lowercased text,
		// so two raw strings that only differ in case, or that sanitize
		// to the same result (e.g. two different characters the sanitizer
		// both replace with '_'), get distinct path_elements with an
		// identical hash. That's harmless: two directories folding
		// together on disk loses nothing, unlike a file colliding with
		// anything, so it's never treated as a real collision below --
		// only a *file* landing on an already-seen hash is.
		files.reserve(eh.crc.size() + aux::numeric_cast<std::size_t>(fs.num_files()));
		for (auto const idx : eh.is_dir.range())
			if (eh.is_dir[idx])
				files.insert({eh.crc[idx], {file_index_t{-1}, idx}});

		// keep track of the total number of name collisions. If there are too
		// many, it's probably a malicious torrent and we should just fail
		int num_collisions = 0;
		for (auto const i : fs.file_range())
		{
			// pad files never touch disk, so a naming collision involving
			// one is never a real conflict: pad files are allowed to
			// collide with each other, and with a real file (or the
			// directory it implies) without the real file being renamed.
			// Skipping them here means they're simply never inserted into
			// `files` either, so nothing else ever needs to check for them.
			if (fs.pad_file_at(i))
				continue;

			// as long as this file already exists
			// increase the counter
			std::uint32_t const hash = fs.file_hash(eh, i);
			auto range = files.equal_range(hash);
			// the hash bucket is empty for the vast majority of files (no
			// other file or directory hashes to this value), so don't
			// reconstruct this file's own path unless there's actually
			// something to compare it against
			if (range.first == range.second)
			{
				files.insert({hash, {i, aux::path_index_t{}}});
				continue;
			}

			std::string const this_name = fs.file_path(i);
			auto const match = std::find_if(
				range.first, range.second, [&](std::pair<std::uint32_t, name_entry> const& o) {
					std::string const other_name = o.second.idx < file_index_t{}
						? fs.internal_directory_path(o.second.dir)
						: fs.file_path(o.second.idx);
					return aux::string_equal_no_case(other_name, this_name);
				});

			if (match == range.second)
			{
				files.insert({hash, {i, aux::path_index_t{}}});
				continue;
			}

			std::string filename = this_name;
			std::string base = remove_extension(filename);
			std::string ext = extension(filename);
			int cnt = 0;
			for (;;)
			{
				++cnt;
				char new_ext[50];
				std::snprintf(new_ext, sizeof(new_ext), ".%d%s", cnt, ext.c_str());
				filename = base + new_ext;

				boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
				process_string_lowercase(crc, filename);
				std::uint32_t const new_hash = crc.checksum();
				if (files.find(new_hash) == files.end())
				{
					files.insert({new_hash, {i, aux::path_index_t{}}});
					break;
				}
				++num_collisions;
				if (num_collisions > max_duplicate_filenames)
				{
					ec = errors::too_many_duplicate_filenames;
					return {};
				}
			}
			ret.insert({i, filename});
		}
		return ret;
	}

}

	std::map<file_index_t, std::string> resolve_duplicate_filenames(
		file_storage const& fs
		, int const max_duplicate_filenames
		, error_code& ec)
	{
		// has_duplicate_filenames() always hashes the whole path tree, even
		// once it's found a collision, since resolve_duplicate_filenames_slow()
		// below needs every file's and directory's hash to find and rename
		// all conflicts, not just the first one.
		auto eh = fs.has_duplicate_filenames();
		if (!eh)
			return {};
		return resolve_duplicate_filenames_slow(fs, *eh, max_duplicate_filenames, ec);
	}

}
