/*

Copyright (c) 2008-2010, 2012-2021, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2017, 2019, Steven Siloti
Copyright (c) 2021, Mark Scott
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_ENTRY_HPP_INCLUDED
#define TORRENT_FILE_ENTRY_HPP_INCLUDED

#include <string>
#include <tuple>
#include <cstdint>

#include "libtorrent/assert.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/fwd.hpp"

namespace libtorrent::aux {

	struct path_index_tag;
	using path_index_t = aux::strong_typedef<std::uint32_t, path_index_tag>;

	// internal
	struct file_entry
	{
		friend class ::lt::file_storage;
		file_entry();
		file_entry(file_entry const& fe);
		file_entry& operator=(file_entry const& fe) &;
		file_entry(file_entry&& fe) noexcept;
		file_entry& operator=(file_entry&& fe) & noexcept;
		~file_entry();

		void set_name(string_view n, bool borrow_string = false);
		string_view filename() const;

		enum {
			name_is_owned = (1 << 12) - 1,
			not_a_symlink = (1 << 15) - 1,
		};

		static inline constexpr aux::path_index_t no_path{(1 << 30) - 1};
		static inline constexpr aux::path_index_t path_is_absolute{(1 << 30) - 2};

		// the offset of this file inside the torrent
		std::uint64_t offset:48;

		// index into file_storage::m_symlinks or not_a_symlink
		// if this is not a symlink
		std::uint64_t symlink_index:15;

		// if this is true, don't include m_name as part of the
		// path to this file
		std::uint64_t no_root_dir:1;

		// the size of this file
		std::uint64_t size:48;

		// the number of characters in the name. If this is
		// name_is_owned, name is 0-terminated and owned by this object
		// (i.e. it should be freed in the destructor). If
		// the len is not name_is_owned, the name pointer does not belong
		// to this object, and it's not 0-terminated
		std::uint64_t name_len:12;
		std::uint64_t pad_file:1;
		std::uint64_t hidden_attribute:1;
		std::uint64_t executable_attribute:1;
		std::uint64_t symlink_attribute:1;

		// make it available for logging
	private:
		// This string is not necessarily 0-terminated!
		// that's why it's private, to keep people away from it
		char const* name = nullptr;
	public:
		// the SHA-256 root of the merkle tree for this file
		// this is a pointer into the .torrent file
		char const* root = nullptr;

		// the index into file_storage::m_paths. To get
		// the full path to this file, concatenate the path
		// from that array with the 'name' field in
		// this struct
		// values for path_index include:
		// no_path means no path (i.e. single file torrent)
		// path_is_absolute means the filename
		// in this field contains the full, absolute path
		// to the file
		aux::path_index_t path_index = file_entry::no_path;
	};

	TORRENT_EXTRA_EXPORT
	int calc_num_pieces(file_storage const& fs);

	// this is used when loading v2 torrents that are backwards compatible with
	// v1 torrents. Both v1 and v2 structures must describe the same file layout,
	// this compares the two.
	TORRENT_EXTRA_EXPORT
	bool files_compatible(file_storage const& lhs, file_storage const& rhs);

	// returns the piece range that entirely falls within the specified file. the
	// end piece is one-past the last piece that entirely falls within the file.
	// i.e. They can conveniently be used as loop boundaries. No edge partial
	// pieces will be included.
	TORRENT_EXTRA_EXPORT std::tuple<piece_index_t, piece_index_t>
	file_piece_range_exclusive(file_storage const& fs, file_index_t file);

	// returns the piece range of pieces that overlaps with the specified file.
	// the end piece is one-past the last piece. i.e. They can conveniently be
	// used as loop boundaries.
	TORRENT_EXTRA_EXPORT std::tuple<piece_index_t, piece_index_t>
	file_piece_range_inclusive(file_storage const& fs, file_index_t file);

} // namespace libtorrent::aux

#endif // TORRENT_FILE_ENTRY_HPP_INCLUDED
