/*

Copyright (c) 2008-2010, 2012-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2017, 2019, Steven Siloti
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
#include "libtorrent/error_code.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/fwd.hpp"

namespace libtorrent {

#if TORRENT_ABI_VERSION == 1
	// information about a file in a file_storage
	struct TORRENT_DEPRECATED_EXPORT file_entry
	{
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"
		// hidden
		file_entry();
		// hidden
		~file_entry();
		file_entry(file_entry const&) = default;
		file_entry& operator=(file_entry const&) & = default;
		file_entry(file_entry&&) noexcept = default;
		file_entry& operator=(file_entry&&) & = default;

#include "libtorrent/aux_/disable_warnings_pop.hpp"

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

namespace aux {
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

		static constexpr aux::path_index_t no_path{(1 << 30) - 1};
		static constexpr aux::path_index_t path_is_absolute{(1 << 30) - 2};

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

} // aux namespace

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
	public:
		// hidden
		file_storage();
		// hidden
		~file_storage();
		file_storage(file_storage const&);
		file_storage& operator=(file_storage const&) &;
		file_storage(file_storage&&) noexcept;
		file_storage& operator=(file_storage&&) &;

		// internal limitations restrict file sizes to not be larger than this
		// We use int to index into file merkle trees, so a file may not contain more
		// than INT_MAX entries. That means INT_MAX / 2 blocks (leafs) in each
		// tree.
		static constexpr std::int64_t max_file_size = std::min(
			(std::int64_t(1) << 48) - 1
			, std::int64_t(std::numeric_limits<int>::max() / 2) * default_block_size);
		static constexpr std::int64_t max_file_offset = (std::int64_t(1) << 48) - 1;

		// returns true if the piece length has been initialized
		// on the file_storage. This is typically taken as a proxy
		// of whether the file_storage as a whole is initialized or
		// not.
		bool is_valid() const { return m_piece_length > 0; }

#if TORRENT_ABI_VERSION == 1
		using flags_t = file_flags_t;
		TORRENT_DEPRECATED static constexpr file_flags_t pad_file = 0_bit;
		TORRENT_DEPRECATED static constexpr file_flags_t attribute_hidden = 1_bit;
		TORRENT_DEPRECATED static constexpr file_flags_t attribute_executable = 2_bit;
		TORRENT_DEPRECATED static constexpr file_flags_t attribute_symlink = 3_bit;
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
		//
		// The overloads that take an `error_code` reference will report failures
		// via that variable, otherwise `system_error` is thrown.
#ifndef BOOST_NO_EXCEPTIONS
		void add_file_borrow(string_view filename
			, std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}, char const* filehash = nullptr
			, std::int64_t mtime = 0, string_view symlink_path = string_view()
			, char const* root_hash = nullptr);
		void add_file(std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}
			, std::time_t mtime = 0, string_view symlink_path = string_view()
			, char const* root_hash = nullptr);
#endif // BOOST_NO_EXCEPTIONS
		void add_file_borrow(error_code& ec, string_view filename
			, std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}, char const* filehash = nullptr
			, std::int64_t mtime = 0, string_view symlink_path = string_view()
			, char const* root_hash = nullptr);
		void add_file(error_code& ec, std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}
			, std::time_t mtime = 0, string_view symlink_path = string_view()
			, char const* root_hash = nullptr);

		// renames the file at ``index`` to ``new_filename``. Keep in mind
		// that filenames are expected to be UTF-8 encoded.
		void rename_file(file_index_t index, std::string const& new_filename);

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

		TORRENT_DEPRECATED
		void add_file_borrow(char const* filename, int filename_len
			, std::string const& path, std::int64_t file_size
			, file_flags_t file_flags = {}, char const* filehash = nullptr
			, std::int64_t mtime = 0, string_view symlink_path = string_view());
		TORRENT_DEPRECATED
		void add_file(file_entry const& fe, char const* filehash = nullptr);

		// all functions depending on aux::file_entry
		// were deprecated in 1.0. Use the variants that take an
		// index instead
		using iterator = std::vector<aux::file_entry>::const_iterator;
		using reverse_iterator = std::vector<aux::file_entry>::const_reverse_iterator;

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
		aux::file_entry const& internal_at(int const index) const;
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

#include "libtorrent/aux_/disable_warnings_pop.hpp"
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
			, std::int64_t size) const;

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

		// set and get the size of each piece in this torrent. It must be a power of two
		// and at least 16 kiB.
		void set_piece_length(int l)  { m_piece_length = l; }
		int piece_length() const { TORRENT_ASSERT(m_piece_length > 0); return m_piece_length; }

		// returns the piece size of ``index``. This will be the same as piece_length(), except
		// for the last piece, which may be shorter.
		int piece_size(piece_index_t index) const;

		// Returns the size of the given piece. If the piece spans multiple files,
		// only the first file is considered part of the piece. This is used for
		// v2 torrents, where all files are piece aligned and padded. i.e. The pad
		// files are not considered part of the piece for this purpose.
		int piece_size2(piece_index_t index) const;

		// returns the number of blocks in the specified piece, for v2 torrents.
		int blocks_in_piece2(piece_index_t index) const;

		// set and get the name of this torrent. For multi-file torrents, this is also
		// the name of the root directory all the files are stored in.
		void set_name(std::string const& n) { m_name = n; }
		std::string const& name() const { return m_name; }

		// swap all content of *this* with *ti*.
		void swap(file_storage& ti) noexcept;

		// arrange files and padding to match the canonical form required
		// by BEP 52
		void canonicalize();

		// These functions are used to query attributes of files at
		// a given index.
		//
		// The ``hash()`` is a SHA-1 hash of the file, or 0 if none was
		// provided in the torrent file. This can potentially be used to
		// join a bittorrent network with other file sharing networks.
		//
		// ``root()`` returns the SHA-256 merkle tree root of the specified file,
		// in case this is a v2 torrent. Otherwise returns zeros.
		// ``root_ptr()`` returns a pointer to the SHA-256 merkle tree root hash
		// for the specified file. The pointer points into storage referred to
		// when the file was added, it is not owned by this object. Torrents
		// that are not v2 torrents return nullptr.
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
		sha256_hash root(file_index_t index) const;
		char const* root_ptr(file_index_t const index) const;
		std::string symlink(file_index_t index) const;
		std::time_t mtime(file_index_t index) const;
		std::string file_path(file_index_t index, std::string const& save_path = "") const;
		string_view file_name(file_index_t index) const;
		std::int64_t file_size(file_index_t index) const;
		bool pad_file_at(file_index_t index) const;
		std::int64_t file_offset(file_index_t index) const;

		// Returns the number of pieces or blocks the file at `index` spans,
		// under the assumption that the file is aligned to the start of a piece.
		// This is only meaningful for v2 torrents, where files are guaranteed
		// such alignment.
		// These numbers are used to size and navigate the merkle hash tree for
		// each file.
		int file_num_pieces(file_index_t index) const;
		int file_num_blocks(file_index_t index) const;
		index_range<piece_index_t::diff_type> file_piece_range(file_index_t) const;

		// index of first piece node in the merkle tree
		int file_first_piece_node(file_index_t index) const;
		int file_first_block_node(file_index_t index) const;

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

		// internal
		// returns all directories used in the torrent. Files in the torrent are
		// located in one of these directories. This is not a tree, it's a flat
		// list of all *leaf* directories. i.e. the union of the parent paths of
		// all files.
		aux::vector<std::string, aux::path_index_t> const& paths() const { return m_paths; }

		// returns a bitmask of flags from file_flags_t that apply
		// to file at ``index``.
		file_flags_t file_flags(file_index_t index) const;

		// returns true if the file at the specified index has been renamed to
		// have an absolute path, i.e. is not anchored in the save path of the
		// torrent.
		bool file_absolute_path(file_index_t index) const;

		// returns the index of the file at the given offset in the torrent
		file_index_t file_index_at_offset(std::int64_t offset) const;
		file_index_t file_index_at_piece(piece_index_t piece) const;

		// finds the file with the given root hash and returns its index
		// if there is no file with the root hash, file_index_t{-1} is returned
		file_index_t file_index_for_root(sha256_hash const& root_hash) const;

		// returns the piece index the given file starts at
		piece_index_t piece_index_at_file(file_index_t f) const;

#if TORRENT_USE_INVARIANT_CHECKS
		// internal
		bool owns_name(file_index_t const f) const
		{ return m_files[f].name_len == aux::file_entry::name_is_owned; }
#endif

#if TORRENT_ABI_VERSION <= 2
		// low-level function. returns a pointer to the internal storage for
		// the filename. This string may not be 0-terminated!
		// the ``file_name_len()`` function returns the length of the filename.
		// prefer to use ``file_name()`` instead, which returns a ``string_view``.
		TORRENT_DEPRECATED
		char const* file_name_ptr(file_index_t index) const;
		TORRENT_DEPRECATED
		int file_name_len(file_index_t index) const;
#endif

#if TORRENT_ABI_VERSION == 1
		// these were deprecated in 1.0. Use the versions that take an index instead
		TORRENT_DEPRECATED
		sha1_hash hash(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::string symlink(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::time_t mtime(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		int file_index(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::string file_path(aux::file_entry const& fe, std::string const& save_path = "") const;
		TORRENT_DEPRECATED
		std::string file_name(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::int64_t file_size(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		bool pad_file_at(aux::file_entry const& fe) const;
		TORRENT_DEPRECATED
		std::int64_t file_offset(aux::file_entry const& fe) const;
#endif

		// validate any symlinks, to ensure they all point to
		// other files or directories inside this storage. Any invalid symlinks
		// are updated to point to themselves.
		void sanitize_symlinks();

		// returns true if this torrent contains v2 metadata.
		bool v2() const { return m_v2; }

		// internal
		// this is an optimization for create_torrent
		std::string const& internal_symlink(file_index_t index) const;

	private:

		std::string internal_file_path(file_index_t index) const;
		file_index_t last_file() const noexcept;

		aux::path_index_t get_or_add_path(string_view path);

		// the number of bytes in a regular piece
		// (i.e. not the potentially truncated last piece)
		int m_piece_length = 0;

		// the number of pieces in the torrent
		int m_num_pieces = 0;

		// whether this is a v2 torrent or not. Additional requirements apply to
		// v2 torrents
		bool m_v2 = false;

		void update_path_index(aux::file_entry& e, std::string const& path
			, bool set_name = true);

		// the list of files that this torrent consists of
		aux::vector<aux::file_entry, file_index_t> m_files;

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
		// path_index in the aux::file_entry indexes
		// this vector of strings
		std::vector<std::string> m_symlinks;

		// the modification times of each file. This vector
		// is empty if no file have a modification time.
		// each element corresponds to the file with the same
		// index in m_files
		aux::vector<std::time_t, file_index_t> m_mtime;

		// all unique paths files have. The aux::file_entry::path_index
		// points into this array. The paths don't include the root directory
		// name for multi-file torrents. The m_name field need to be
		// prepended to these paths, and the filename of a specific file
		// entry appended, to form full file paths
		aux::vector<std::string, aux::path_index_t> m_paths;

		// name of torrent. For multi-file torrents
		// this is always the root directory
		std::string m_name;

		// the sum of all file sizes
		std::int64_t m_total_size = 0;
	};

namespace aux {

	TORRENT_EXTRA_EXPORT
	int calc_num_pieces(file_storage const& fs);

	// this is used when loading v2 torrents that are backwards compatible with
	// v1 torrents. Both v1 and v2 structures must describe the same file layout,
	// this compares the two.
	TORRENT_EXTRA_EXPORT
	bool files_equal(file_storage const& lhs, file_storage const& rhs);

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
