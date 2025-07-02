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

#ifdef TORRENT_WINDOWS
#define TORRENT_SEPARATOR '\\'
#else
#define TORRENT_SEPARATOR '/'
#endif

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
		file_index_t idx;
		int length;
	};

	std::map<file_index_t, std::string> resolve_duplicate_filenames_slow(
		file_storage const& fs
		, int const max_duplicate_filenames
		, error_code& ec)
	{
		// maps filename hash to file index
		// or, if the file_index is negative, maps into the paths vector
		std::unordered_multimap<std::uint32_t, name_entry> files;

		std::map<file_index_t, std::string> ret;

		std::vector<std::string> const& paths = fs.paths();
		files.reserve(paths.size() + aux::numeric_cast<std::size_t>(fs.num_files()));

		// insert all directories first, to make sure no files
		// are allowed to collied with them
		{
			boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
			if (!fs.name().empty())
			{
				process_string_lowercase(crc, fs.name());
			}
			file_index_t path_index{-1};
			for (auto const& path : paths)
			{
				auto local_crc = crc;
				if (!path.empty()) local_crc.process_byte(TORRENT_SEPARATOR);
				int count = 0;
				for (char const c : path)
				{
					if (c == TORRENT_SEPARATOR)
						files.insert({local_crc.checksum(), {path_index, count}});
					local_crc.process_byte(aux::to_lower(c) & 0xff);
					++count;
				}
				files.insert({local_crc.checksum(), {path_index, int(path.size())}});
				--path_index;
			}
		}

		// keep track of the total number of name collisions. If there are too
		// many, it's probably a malicious torrent and we should just fail
		int num_collisions = 0;
		for (auto const i : fs.file_range())
		{
			// as long as this file already exists
			// increase the counter
			std::uint32_t const hash = fs.file_path_hash(i, "");
			auto range = files.equal_range(hash);
			auto const match = std::find_if(range.first, range.second, [&](std::pair<std::uint32_t, name_entry> const& o)
			{
				std::string const other_name = o.second.idx < file_index_t{}
					? combine_path(fs.name(), paths[std::size_t(-static_cast<int>(o.second.idx)-1)].substr(0, std::size_t(o.second.length)))
					: fs.file_path(o.second.idx);
				return aux::string_equal_no_case(other_name, fs.file_path(i));
			});

			if (match == range.second)
			{
				files.insert({hash, {i, 0}});
				continue;
			}

			// pad files are allowed to collide with each-other, as long as they have
			// the same size.
			file_index_t const other_idx = match->second.idx;
			if (other_idx >= file_index_t{}
				&& (fs.file_flags(i) & file_storage::flag_pad_file)
				&& (fs.file_flags(other_idx) & file_storage::flag_pad_file)
				&& fs.file_size(i) == fs.file_size(other_idx))
				continue;

			std::string filename = fs.file_path(i);
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
					files.insert({new_hash, {i, 0}});
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
		// TODO: this can be more efficient for v2 torrents
		std::unordered_set<std::uint32_t> files;

		std::string const empty_str;

		// insert all directories first, to make sure no files
		// are allowed to collied with them
		fs.all_path_hashes(files);
		for (auto const i : fs.file_range())
		{
			// as long as this file already exists
			// increase the counter
			std::uint32_t const h = fs.file_path_hash(i, empty_str);
			if (!files.insert(h).second)
			{
				// This filename appears to already exist!
				// If this happens, just start over and do it the slow way,
				// comparing full file names and come up with new names
				return resolve_duplicate_filenames_slow(fs, max_duplicate_filenames, ec);
			}
		}
		return {};
	}

}
