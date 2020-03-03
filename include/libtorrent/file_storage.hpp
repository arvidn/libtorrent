/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_FILE_STORAGE_HPP_INCLUDED
#define TORRENT_FILE_STORAGE_HPP_INCLUDED


#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <ctime>
#include <cstdint>

#include "libtorrent/assert.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/index_range.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

#if TORRENT_ABI_VERSION == 1
	// information about a file in a file_storage
	struct TORRENT_DEPRECATED_EXPORT file_entry
	{
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		// hidden
		file_entry();
		// hidden
		~file_entry();
		file_entry(file_entry const&) = default;
		file_entry& operator=(file_entry const&) & = default;
		file_entry(file_entry&&) noexcept = default;
		file_entry& operator=(file_entry&&) & noexcept = default;

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

		// the full path of this file. The paths are unicode strings
		// encoded in UTF-8.
		std::string path;

		// the path which this is a symlink to, or empty if this is
		// not a symlink. This field is only used if the ``symlink_attribute`` is set.
		std::string symlink_path;

		// the offset of this file inside the torrent
		std::int64_t offset;

		// the size of the file (in bytes) and ``offset`` is the byte offset
		// of the file within the torrent. i.e. the sum of all the sizes of the files
		// before it in the list.
		std::int64_t size;

		// the modification time of this file specified in posix time.
		std::time_t mtime;

		// a SHA-1 hash of the content of the file, or zeros, if no
		// file hash was present in the torrent file. It can be used to potentially
		// find alternative sources for the file.
		sha1_hash filehash;

		// set to true for files that are not part of the data of the torrent.
		// They are just there to make sure the next file is aligned to a particular byte offset
		// or piece boundary. These files should typically be hidden from an end user. They are
		// not written to disk.
		bool pad_file:1;

		// true if the file was marked as hidden (on windows).
		bool hidden_attribute:1;

		// true if the file was marked as executable (posix)
		bool executable_attribute:1;

		// true if the file was a symlink. If this is the case
		// the ``symlink_index`` refers to a string which specifies the original location
		// where the data for this file was found.
		bool symlink_attribute:1;
	};
#endif // TORRENT_ABI_VERSION

	// internal
	struct internal_file_entry
	{
		friend class file_storage;
#if TORRENT_USE_INVARIANT_CHECKS
		// for torrent_info::invariant_check
		friend class torrent_info;
#endif

		internal_file_entry();
		internal_file_entry(internal_file_entry const& fe);
		internal_file_entry& operator=(internal_file_entry const& fe) &;
		internal_file_entry(internal_file_entry&& fe) noexcept;
		internal_file_entry& operator=(internal_file_entry&& fe) & noexcept;
		~internal_file_entry();

		void set_name(string_view n, bool borrow_string = false);
		string_view filename() const;

		enum {
			name_is_owned = (1 << 12) - 1,
			not_a_symlink = (1 << 15) - 1
		};

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
		char const* name;
	public:

		// the index into file_storage::m_paths. To get
		// the full path to this file, concatenate the path
		// from that array with the 'name' field in
		// this struct
		// values for path_index include:
		// -1 means no path (i.e. single file torrent)
		// -2, it means the filename
		// in this field contains the full, absolute path
		// to the file
		int path_index;
	};


	// represents a window of a file in a torrent.
	//
	// The ``file_index`` refers to the index of the file (in the torrent_info).
	// To get the path and filename, use ``file_path()`` and give the ``file_index``
	// as argument. The ``offset`` is the byte offset in the file where the range
	// starts, and ``size`` is the number of bytes this range is. The size + offset
	// will never be greater than the file size.
	struct TORRENT_EXPORT file_slice
	{
		// the index of the file
		file_index_t file_index;

		// the offset from the start of the file, in bytes
		std::int64_t offset;

		// the size of the window, in bytes
		std::int64_t size;
	};

	// hidden
	using file_flags_t = flags::bitfield_flag<std::uint8_t, struct file_flags_tag>;

	// The ``file_storage`` class represents a file list and the piece
	// size. Everything necessary to interpret a regular bittorrent storage
	// file structure.
	class TORRENT_EXPORT file_storage
	{
	friend class torrent_info;
	public:
		// hidden
		file_storage();
		// hidden
		~file_storage();
		file_storage(file_storage const&);
		file_storage& operator=(file_storage const&);
		file_storage(file_storage&&) noexcept;
		file_storage& operator=(file_storage&&) = default;

		// returns true if the piece length has been initialized
		// on the file_storage. This is typically taken as a proxy
		// of whether the file_storage as a whole is initialized or
		// not.
		bool is_valid() const { return m_piece_length > 0; }

#if TORRENT_ABI_VERSION == 1
		using flags_t = file_flags_t;
		static constexpr file_flags_t TORRENT_DEPRECATED_MEMBER pad_file = 0_bit;
		static constexpr file_flags_t TORRENT_DEPRECATED_MEMBER attribute_hidden = 1_bit;
		static constexpr file_flags_t TORRENT_DEPRECATED_MEMBER attribute_executable = 2_bit;
		static constexpr file_flags_t TORRENT_DEPRECATED_MEMBER attribute_symlink = 3_bit;
#endif

		// allocates space for ``num_files`` in the internal file list. This can
		// be used to avoid reallocating the internal file list when the number
		// of files to be added is known up-front.
		void reserve(int num_files);

		// Adds a file to the file storage. The ``add_file_borrow`` version
		// expects that ``filename`` is the file name (without a path) of
		// the file that's being added.
		// This memory is *borrowed*, i.e. it is the caller's
		// responsibility to make sure it stays valid throughout the lifetime
		// of this file_storage object or any copy of it. The same thing applies
		// to ``filehash``, which is an optional pointer to a 20 byte binary
		// SHA-1 hash of the file.
		//
		// if ``filename`` is empty, the filename from ``path`` is used and not
		// borrowed.
		//
		// The ``path`` argument is the full path (in the torrent file) to
		// the file to add. Note that this is not supposed to be an absolute
		// path, but it is expected to include the name of the torrent as the
		// first path element.
		//
		// ``file_size`` is the size of the file in bytes.
		//
		// The ``file_flags`` argument sets attributes on the file. The file
		// attributes is an extension and may not work in all bittorrent clients.
		//
		// For possible file attributes, see file_storage::flags_t.
		//
		// The ``mtime`` argument is optional and can be set to 0. If non-zero,
		// it is the posix time of the last modification time of this file.
		//
		// ``symlink_path`` is the path the file is a symlink to. To make this a
		// symlink you also need to set the file_storage::flag_symlink file flag.
		//
		// If more files than one are added, certain restrictions to their paths
		// apply. In a multi-file file storage (torrent), all files must share
		// the same root directory.
		//
		// That is, the first path element of all files must be the same.
		// This shared path element is also set to the name of the torrent. It
		// can be changed by calling ``set_name``.
		void add_file_borrow(string_view filename
			, std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}, char const* filehash = nullptr
			, std::int64_t mtime = 0, string_view symlink_path = string_view());
		void add_file(std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}
			, std::time_t mtime = 0, string_view symlink_path = string_view());

		// renames the file at ``index`` to ``new_filename``. Keep in mind
		// that filenames are expected to be UTF-8 encoded.
		void rename_file(file_index_t index, std::string const& new_filename);

#if TORRENT_ABI_VERSION == 1
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(push, 1)
#pragma warning(disable: 4996)
#endif
		TORRENT_DEPRECATED
		void add_file_borrow(char const* filename, int filename_len
			, std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}, char const* filehash = nullptr
			, std::int64_t mtime = 0, string_view symlink_path = string_view());
		TORRENT_DEPRECATED
		void add_file(file_entry const& fe, char const* filehash = nullptr);

		// all wstring APIs are deprecated since 0.16.11
		// instead, use the wchar -> utf8 conversion functions
		// and pass in utf8 strings
		TORRENT_DEPRECATED
		void add_file(std::wstring const& p, std::int64_t size
			, file_flags_t flags = {}
			, std::time_t mtime = 0, string_view s_p = "");
		TORRENT_DEPRECATED
		void rename_file(file_index_t index, std::wstring const& new_filename);
		TORRENT_DEPRECATED
		void set_name(std::wstring const& n);

		void rename_file_deprecated(file_index_t index, std::wstring const& new_filename);
		// all functions depending on internal_file_entry
		// were deprecated in 1.0. Use the variants that take an
		// index instead
		using iterator = std::vector<internal_file_entry>::const_iterator;
		using reverse_iterator = std::vector<internal_file_entry>::const_reverse_iterator;

		TORRENT_DEPRECATED
		iterator file_at_offset(std::int64_t offset) const;
		TORRENT_DEPRECATED
		iterator begin() const { return m_files.begin(); }
		TORRENT_DEPRECATED
		iterator end() const { return m_files.end(); }
		TORRENT_DEPRECATED
		reverse_iterator rbegin() const { return m_files.rbegin(); }
		TORRENT_DEPRECATED
		reverse_iterator rend() const { return m_files.rend(); }
		TORRENT_DEPRECATED
		internal_file_entry const& internal_at(int const index) const;
		TORRENT_DEPRECATED
		file_entry at(iterator i) const;

		// returns a file_entry with information about the file
		// at ``index``. Index must be in the range [0, ``num_files()`` ).
		TORRENT_DEPRECATED
		file_entry at(int index) const;

		iterator begin_deprecated() const { return m_files.begin(); }
		iterator end_deprecated() const { return m_files.end(); }
		reverse_iterator rbegin_deprecated() const { return m_files.rbegin(); }
		reverse_iterator rend_deprecated() const { return m_files.rend(); }
		iterator file_at_offset_deprecated(std::int64_t offset) const;
		file_entry at_deprecated(int index) const;

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // TORRENT_ABI_VERSION

		// returns a list of file_slice objects representing the portions of
		// files the specified piece index, byte offset and size range overlaps.
		// this is the inverse mapping of map_file().
		//
		// Preconditions of this function is that the input range is within the
		// torrents address space. ``piece`` may not be negative and
		//
		// 	``piece`` * piece_size + ``offset`` + ``size``
		//
		// may not exceed the total size of the torrent.
		std::vector<file_slice> map_block(piece_index_t piece, std::int64_t offset
			, int size) const;

		// returns a peer_request representing the piece index, byte offset
		// and size the specified file range overlaps. This is the inverse
		// mapping over map_block(). Note that the ``peer_request`` return type
		// is meant to hold bittorrent block requests, which may not be larger
		// than 16 kiB. Mapping a range larger than that may return an overflown
		// integer.
		peer_request map_file(file_index_t file, std::int64_t offset, int size) const;

		// returns the number of files in the file_storage
		int num_files() const noexcept;

		// returns the index of the one-past-end file in the file storage
		file_index_t end_file() const noexcept;

		// returns an implementation-defined type that can be used as the
		// container in a range-for loop. Where the values are the indices of all
		// files in the file_storage.
		index_range<file_index_t> file_range() const noexcept;

		// returns the total number of bytes all the files in this torrent spans
		std::int64_t total_size() const { return m_total_size; }

		// set and get the number of pieces in the torrent
		void set_num_pieces(int n) { m_num_pieces = n; }
		int num_pieces() const { TORRENT_ASSERT(m_piece_length > 0); return m_num_pieces; }

		// returns the index of the one-past-end piece in the file storage
		piece_index_t end_piece() const
		{ return piece_index_t(m_num_pieces); }

		// returns the index of the last piece in the torrent. The last piece is
		// special in that it may be smaller than the other pieces (and the other
		// pieces are all the same size).
		piece_index_t last_piece() const
		{ return piece_index_t(m_num_pieces - 1); }

		// returns an implementation-defined type that can be used as the
		// container in a range-for loop. Where the values are the indices of all
		// pieces in the file_storage.
		index_range<piece_index_t> piece_range() const noexcept;

		// set and get the size of each piece in this torrent. This size is typically an even power
		// of 2. It doesn't have to be though. It should be divisible by 16 kiB however.
		void set_piece_length(int l)  { m_piece_length = l; }
		int piece_length() const { TORRENT_ASSERT(m_piece_length > 0); return m_piece_length; }

		// returns the piece size of ``index``. This will be the same as piece_length(), except
		// for the last piece, which may be shorter.
		int piece_size(piece_index_t index) const;

		// set and get the name of this torrent. For multi-file torrents, this is also
		// the name of the root directory all the files are stored in.
		void set_name(std::string const& n) { m_name = n; }
		std::string const& name() const { return m_name; }

		// swap all content of *this* with *ti*.
		void swap(file_storage& ti) noexcept;

		// if pad_file_limit >= 0, files larger than that limit will be padded,
		// default is to not add any padding (-1). The alignment specifies the
		// alignment files should be padded to. This defaults to the piece size
		// (-1) but it may also make sense to set it to 16 kiB, or something
		// divisible by 16 kiB.
		// If pad_file_limit is 0, every file will be padded (except empty ones).
		// ``tail_padding`` indicates whether aligned files also are padded at
		// the end to make them end aligned. This is required for mutable
		// torrents, since piece hashes are compared
		void optimize(int pad_file_limit = -1, int alignment = -1
			, bool tail_padding = false);

		// These functions are used to query attributes of files at
		// a given index.
		//
		// The ``hash()`` is a SHA-1 hash of the file, or 0 if none was
		// provided in the torrent file. This can potentially be used to
		// join a bittorrent network with other file sharing networks.
		//
		// The ``mtime()`` is the modification time is the posix
		// time when a file was last modified when the torrent
		// was created, or 0 if it was not included in the torrent file.
		//
		// ``file_path()`` returns the full path to a file.
		//
		// ``file_size()`` returns the size of a file.
		//
		// ``pad_file_at()`` returns true if the file at the given
		// index is a pad-file.
		//
		// ``file_name()`` returns *just* the name of the file, whereas
		// ``file_path()`` returns the path (inside the torrent file) with
		// the filename appended.
		//
		// ``file_offset()`` returns the byte offset within the torrent file
		// where this file starts. It can be used to map the file to a piece
		// index (given the piece size).
		sha1_hash hash(file_index_t index) const;
		std::string const& symlink(file_index_t index) const;
		std::time_t mtime(file_index_t index) const;
		std::string file_path(file_index_t index, std::string const& save_path = "") const;
		string_view file_name(file_index_t index) const;
		std::int64_t file_size(file_index_t index) const;
		bool pad_file_at(file_index_t index) const;
		std::int64_t file_offset(file_index_t index) const;

		// returns the crc32 hash of file_path(index)
		std::uint32_t file_path_hash(file_index_t index, std::string const& save_path) const;

		// this will add the CRC32 hash of all directory entries to the table. No
		// filename will be included, just directories. Every depth of directories
		// are added separately to allow test for collisions with files at all
		// levels. i.e. if one path in the torrent is ``foo/bar/baz``, the CRC32
		// hashes for ``foo``, ``foo/bar`` and ``foo/bar/baz`` will be added to
		// the set.
		void all_path_hashes(std::unordered_set<std::uint32_t>& table) const;

		// the file is a pad file. It's required to contain zeros
		// at it will not be saved to disk. Its purpose is to make
		// the following file start on a piece boundary.
		static constexpr file_flags_t flag_pad_file = 0_bit;

		// this file has the hidden attribute set. This is primarily
		// a windows attribute
		static constexpr file_flags_t flag_hidden = 1_bit;

		// this file has the executable attribute set.
		static constexpr file_flags_t flag_executable = 2_bit;

		// this file is a symbolic link. It should have a link
		// target string associated with it.
		static constexpr file_flags_t flag_symlink = 3_bit;

		// returns all directories used in the torrent. Files in the torrent are
		// located in one of these directories. This is not a tree, it's a flat
		// list of all *leaf* directories. i.e. the union of the parent paths of
		// all files.
		std::vector<std::string> const& paths() const { return m_paths; }

		// returns a bitmask of flags from file_flags_t that apply
		// to file at ``index``.
		file_flags_t file_flags(file_index_t index) const;

		// returns true if the file at the specified index has been renamed to
		// have an absolute path, i.e. is not anchored in the save path of the
		// torrent.
		bool file_absolute_path(file_index_t index) const;

		// returns the index of the file at the given offset in the torrent
		file_index_t file_index_at_offset(std::int64_t offset) const;

		// low-level function. returns a pointer to the internal storage for
		// the filename. This string may not be 0-terminated!
		// the ``file_name_len()`` function returns the length of the filename.
		// prefer to use ``file_name()`` instead, which returns a ``string_view``.
		char const* file_name_ptr(file_index_t index) const;
		int file_name_len(file_index_t index) const;

#if TORRENT_ABI_VERSION == 1
		// these were deprecated in 1.0. Use the versions that take an index instead
		TORRENT_DEPRECATED
		sha1_hash hash(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::string const& symlink(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::time_t mtime(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		int file_index(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::string file_path(internal_file_entry const& fe, std::string const& save_path = "") const;
		TORRENT_DEPRECATED
		std::string file_name(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::int64_t file_size(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		bool pad_file_at(internal_file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::int64_t file_offset(internal_file_entry const& fe) const;
#endif

		// if the backing buffer changed for this storage, this is the pointer
		// offset to add to any pointers to make them point into the new buffer
		void apply_pointer_offset(std::ptrdiff_t off);

		// validate any symlinks, to ensure they all point to
		// other files or directories inside this storage. Any invalid symlinks
		// are updated to point to themselves.
		void sanitize_symlinks();

	private:

		std::string internal_file_path(file_index_t index) const;
		file_index_t last_file() const noexcept;

		int get_or_add_path(string_view path);

		void add_pad_file(int size
			, std::vector<internal_file_entry>::iterator& i
			, std::int64_t& offset
			, int& pad_file_counter);

		// the number of bytes in a regular piece
		// (i.e. not the potentially truncated last piece)
		int m_piece_length;

		// the number of pieces in the torrent
		int m_num_pieces;

		void update_path_index(internal_file_entry& e, std::string const& path
			, bool set_name = true);
		void reorder_file(int index, int dst);

		// the list of files that this torrent consists of
		aux::vector<internal_file_entry, file_index_t> m_files;

		// if there are sha1 hashes for each individual file there are as many
		// entries in this array as the m_files array. Each entry in m_files has
		// a corresponding hash pointer in this array. The reason to split it up
		// in separate arrays is to save memory in case the torrent doesn't have
		// file hashes
		// the pointers in this vector are pointing into the .torrent file in
		// memory which is _not_ owned by this file_storage object. It's simply
		// a non-owning pointer. It is the user's responsibility that the hash
		// stays valid throughout the lifetime of this file_storage object.
		aux::vector<char const*, file_index_t> m_file_hashes;

		// for files that are symlinks, the symlink
		// path_index in the internal_file_entry indexes
		// this vector of strings
		std::vector<std::string> m_symlinks;

		// the modification times of each file. This vector
		// is empty if no file have a modification time.
		// each element corresponds to the file with the same
		// index in m_files
		aux::vector<std::time_t, file_index_t> m_mtime;

		// all unique paths files have. The internal_file_entry::path_index
		// points into this array. The paths don't include the root directory
		// name for multi-file torrents. The m_name field need to be
		// prepended to these paths, and the filename of a specific file
		// entry appended, to form full file paths
		aux::vector<std::string> m_paths;

		// name of torrent. For multi-file torrents
		// this is always the root directory
		std::string m_name;

		// the sum of all file sizes
		std::int64_t m_total_size;
	};

	namespace aux {

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

} // namespace aux
} // namespace libtorrent

#endif // TORRENT_FILE_STORAGE_HPP_INCLUDED
