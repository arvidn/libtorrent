/*

Copyright (c) 2008-2010, 2012-2021, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2017, 2019, Steven Siloti
Copyright (c) 2021, Mark Scott
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_STORAGE_HPP_INCLUDED
#define TORRENT_FILE_STORAGE_HPP_INCLUDED


#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <unordered_map>
#include <map>
#include <utility>
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

namespace aux {
	struct path_index_tag;
	using path_index_t = aux::strong_typedef<std::uint32_t, path_index_tag>;

	// a file's name. For ordinary files this is a view into a string
	// owned elsewhere, exactly like a string_view. Pad files have no
	// stored name; one is generated from the pad file's size each time
	// this is asked for.
	struct TORRENT_EXPORT file_name_view
	{
		file_name_view(string_view name)
			: m_name(name)
		{} // NOLINT
		explicit file_name_view(std::uint64_t pad_size);

		// converting to string_view or std::string is explicit since, for
		// pad files, the returned string_view may refer to storage owned by
		// this object; the caller must not outlive it when taking a view
		explicit operator string_view() const
		{
			return std::visit([](auto const& n) { return string_view(n); }, m_name);
		}
		explicit operator std::string() const { return std::string(string_view(*this)); }
		char const* data() const { return string_view(*this).data(); }
		std::size_t size() const { return string_view(*this).size(); }

	private:
		std::variant<string_view, std::string> m_name;
	};

	// internal
	// a single path-component node in file_storage's path tree. Both
	// directory components and filenames are represented as path_element
	// nodes; a file's full path is reconstructed by walking ``parent``
	// links from its leaf element up to the root. Only file_storage
	// constructs and resolves these, so all fields are public and mutated
	// directly by file_storage::make_directory().
	struct path_element
	{
		path_element() = default;
		path_element(path_element const&);
		path_element(path_element&&) noexcept;
		path_element& operator=(path_element const&) = delete;
		path_element& operator=(path_element&&) = delete;
		~path_element();

		// sentinel parent for a top-level element
		static inline constexpr path_index_t torrent_root{(std::uint32_t(1) << 30) - 1};
		// sentinel parent for an absolute path; the element's own text is
		// the entire path, never split into components
		static inline constexpr path_index_t path_is_absolute{(std::uint32_t(1) << 30) - 2};
		// sentinel identifying the pad-file directory. Resolves to
		// name()/".pad" without a real path_element: nothing is ever nested
		// under it (pad files reference it directly as their own directory,
		// synthesizing their leaf name from their size on demand), so
		// there's nothing to dedupe or store.
		static inline constexpr path_index_t pad_directory{(std::uint32_t(1) << 30) - 3};
		// sentinel parent for a chain (or a lone element) that doesn't root
		// at file_storage::name(): a single-file torrent's lone file, or a
		// path added/renamed that doesn't share the torrent's own name.
		// name() is not prepended when reconstructing a path under it,
		// unlike a chain rooted at torrent_root.
		static inline constexpr path_index_t no_root_dir{(std::uint32_t(1) << 30) - 4};

		// sentinel name_len meaning name_ptr is an owned, 0-terminated copy
		static inline constexpr std::uint16_t name_is_owned = 0xffff;

		path_index_t parent = path_element::torrent_root;

		// borrowed: name_ptr points to name_len bytes owned by the caller
		// (any buffer, not necessarily file_storage::m_info_section), not
		// necessarily 0-terminated
		// owned (name_len == name_is_owned): name_ptr is heap-allocated,
		// 0-terminated, freed by this object
		std::uint16_t name_len = 0;

		// this element's own depth: 1 for a top-level element (parent is one
		// of the sentinels above), or 1 + parent's depth otherwise. Lets
		// file_storage::reconstruct_path() size its output buffer without a
		// preliminary walk up to the root. Kept the same width as name_len
		// (rather than a wider type) so this struct stays 16 bytes; relies
		// on callers keeping directory nesting within std::uint16_t, see
		// load_torrent_limits::max_directory_depth
		std::uint16_t depth = 0;

		char const* name_ptr = nullptr;
	};

	// internal
	struct file_entry
	{
		// the offset of this file inside the torrent
		std::uint64_t offset = 0;

		// the size of this file. 60 bits, since that's what's left in this
		// word alongside the 4 single-bit flags below
		std::uint64_t size:60 = 0;

		std::uint64_t pad_file:1 = false;
		std::uint64_t hidden_attribute:1 = false;
		std::uint64_t executable_attribute:1 = false;
		std::uint64_t symlink_attribute:1 = false;

		// index into file_storage::m_path_elements. For ordinary files this
		// is the leaf element (its own text is the filename; walk ::parent
		// for the rest of the path). For pad files this is the parent
		// *directory* element instead: pad files never store a name, it's
		// synthesized from ::size on demand, see file_storage::file_name()
		path_index_t path_element_index = path_element::torrent_root;

		// a symlink never has a v2 merkle root (it carries no payload of
		// its own), so these two never need to be meaningful at the same
		// time; symlink_attribute selects which one to interpret
		union
		{
			// offset into file_storage::m_info_section where this file's
			// SHA-256 merkle tree root is stored, or file_storage::no_root_hash
			// if this file has none. Meaningful unless symlink_attribute is set
			std::int32_t root_offset = -1;

			// index into file_storage::m_path_elements identifying this
			// symlink's resolved target: a real file's leaf, a real
			// directory, or another symlink's own leaf. Only meaningful
			// when symlink_attribute is set
			path_index_t symlink_element_index;
		};
	};

	// forward declared so file_storage can grant it friend access; see the
	// definition below the class for documentation
	TORRENT_EXTRA_EXPORT bool files_compatible(file_storage const& lhs, file_storage const& rhs);

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

TORRENT_VERSION_NAMESPACE_4

// The ``file_storage`` class represents a file list and the piece
// size. Everything necessary to interpret a regular bittorrent storage
// file structure. A file storage is tied to the torrent_info object that
// created it, which it should never outlive.
class TORRENT_EXPORT file_storage
{
public:
#if TORRENT_ABI_VERSION >= 5
	TORRENT_UNEXPORT
#endif
	// hidden
	// constructs a file_storage whose v2 (BEP 52) merkle tree root
	// hashes, passed to add_file()/add_file_borrow() as pointers, or to
	// add_file_borrow() as offsets, are resolved relative to
	// ``info_section``. ``info_section`` must out-live this file_storage
	// object and any copy of it.
	file_storage();

	// internal
	TORRENT_UNEXPORT explicit file_storage(char const* info_section);

	// hidden
	~file_storage();
	file_storage(file_storage const&);
	file_storage& operator=(file_storage const&) & = delete;
	file_storage(file_storage&&) noexcept;
	file_storage& operator=(file_storage&&) &;

	// internal limitations restrict file sizes to not be larger than this
	// We use int to index into file merkle trees, so a file may not contain more
	// than INT_MAX entries. That means INT_MAX / 2 blocks (leafs) in each
	// tree.
	static constexpr std::int64_t max_file_size = (std::min)((std::int64_t(1) << 48) - 1,
		std::int64_t((std::numeric_limits<int>::max)() / 2) * default_block_size);
	static constexpr std::int64_t max_file_offset = (std::int64_t(1) << 48) - 1;

	// we use a signed 32 bit integer for piece indices internally, but
	// frequently need headroom for intermediate calculations, so we limit
	// the number of pieces 1 bit below the maximum
	static constexpr std::int32_t max_num_pieces = (std::int32_t(1) << 30) - 1;

	// limit the piece length at (2 ^ 30) to get a bit of headroom. We
	// commonly compute the number of blocks per pieces by adding
	// block_size - 1 before dividing by block_size. That would overflow with
	// a piece size of 2 ^ 31. This limit is still an unreasonably large
	// piece size anyway.
	// The piece picker (currently) has a limit of no more than (2^15)-1
	// blocks per piece, which is more restrictive, at a block size of 16
	// kiB (0x4000).
	static constexpr std::int32_t max_piece_size = ((1 << 15) - 1) * 0x4000;

	// sentinel value for the ``root_hash_offset`` argument to
	// add_file_borrow(), meaning the file has no v2 (BEP 52)
	// merkle tree root hash
	static constexpr std::int32_t no_root_hash = -1;

	// returns true if the piece length has been initialized
	// on the file_storage. This is typically taken as a proxy
	// of whether the file_storage as a whole is initialized or
	// not.
	bool is_valid() const { return m_piece_length > 0; }

#if TORRENT_ABI_VERSION == 1
		using flags_t = file_flags_t;
		TORRENT_DEPRECATED static inline constexpr file_flags_t pad_file = 0_bit;
		TORRENT_DEPRECATED static inline constexpr file_flags_t attribute_hidden = 1_bit;
		TORRENT_DEPRECATED static inline constexpr file_flags_t attribute_executable = 2_bit;
		TORRENT_DEPRECATED static inline constexpr file_flags_t attribute_symlink = 3_bit;
#endif

		// allocates space for ``num_files`` in the internal file list. This can
		// be used to avoid reallocating the internal file list when the number
		// of files to be added is known up-front.
		void reserve(int num_files);

#ifndef BOOST_NO_EXCEPTIONS
		// internal
		// deprecated: prefer make_directory()/add_file()/add_symlink(), the
		// path_index_t-based primitives, which don't need to split a joined
		// path string on every call. ``filename`` is *borrowed*, i.e. it is
		// the caller's responsibility to make sure it stays valid throughout
		// the lifetime of this file_storage object or any copy of it. If
		// ``filename`` is empty, the filename from ``path`` is used and not
		// borrowed. ``root_hash_offset`` is the byte offset of the v2 (BEP
		// 52) merkle tree root hash within the buffer this file_storage was
		// constructed with (see the ``file_storage(char const*)``
		// constructor); use ``file_storage::no_root_hash`` when there is
		// none.
		TORRENT_DEPRECATED
		TORRENT_UNEXPORT void add_file_borrow(string_view filename,
			std::string const& path,
			std::int64_t file_size,
			file_flags_t file_flags = {},
			std::int64_t mtime = 0,
			string_view symlink_path = string_view(),
			std::int32_t root_hash_offset = no_root_hash);

#if TORRENT_ABI_VERSION < 5
		// Adds a file to the file storage.
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
		// ``root_hash`` is an optional pointer to a 32 byte SHA-256 hash, being
		// the merkle tree root hash for this file. This is only used for v2
		// torrents. If the root hash is specified for one file, it has to
		// be specified for all, otherwise this function will fail. The
		// pointer must point into the buffer this file_storage was
		// constructed with (see the ``file_storage(char const*)``
		// constructor); it will not be copied. This parameter is only used
		// when *loading* torrents, that already have their file hashes
		// computed. When creating torrents, the file hashes will be
		// computed by the piece hashes.
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
		TORRENT_DEPRECATED
		void add_file(std::string const& path,
			std::int64_t file_size,
			file_flags_t file_flags = {},
			std::time_t mtime = 0,
			string_view symlink_path = string_view(),
			char const* root_hash = nullptr);
#endif
#endif // BOOST_NO_EXCEPTIONS

		// internal
		// deprecated: prefer make_directory()/add_file()/add_symlink(), see
		// the string_view overload above. Same as that overload, but
		// reports failures via ``ec`` rather than throwing.
		TORRENT_DEPRECATED
		TORRENT_UNEXPORT void add_file_borrow(error_code& ec,
			string_view filename,
			std::string const& path,
			std::int64_t file_size,
			file_flags_t file_flags = {},
			std::int64_t mtime = 0,
			string_view symlink_path = string_view(),
			std::int32_t root_hash_offset = no_root_hash);

#if TORRENT_ABI_VERSION < 5
		// this overload reports failures through the ``ec`` reference rather
		// than throwing.
		TORRENT_DEPRECATED
		void add_file(error_code& ec,
			std::string const& path,
			std::int64_t file_size,
			file_flags_t file_flags = {},
			std::time_t mtime = 0,
			string_view symlink_path = string_view(),
			char const* root_hash = nullptr);
#endif

		// internal
		// creates a new path_element as a child of ``parent`` (aux::path_
		// element::torrent_root for a top-level directory) and returns its
		// index. Used for directory components as well as file and symlink
		// leaves; no dedup lookup, always creates a new entry, so callers
		// doing bulk work (parsing a torrent, walking a directory tree) are
		// expected to cache and reuse indices for repeated (parent, name)
		// pairs themselves. ``name`` is borrowed (the caller guarantees it
		// outlives this file_storage) when ``borrow`` is true, otherwise
		// heap-copied.
		TORRENT_UNEXPORT aux::path_index_t make_directory(
			aux::path_index_t parent, string_view name, bool borrow = false);

		// internal
		// adds a file named ``filename`` directly under directory ``dir``
		// (as returned by make_directory(), aux::path_element::torrent_root
		// for the torrent's own root, or aux::path_element::no_root_dir to
		// omit file_storage::name() when reconstructing this file's path,
		// the single-file-torrent convention). ``filename`` is borrowed
		// (the caller guarantees it outlives this file_storage) when
		// ``borrow`` is true, otherwise heap-copied. ``root_hash_offset``
		// is the byte offset of this file's v2 merkle tree root within the
		// buffer this file_storage was constructed with (see the
		// ``file_storage(char const*)`` constructor), or
		// file_storage::no_root_hash if this file has none. Returns the new
		// file's own path_index_t. Not for symlinks, see add_symlink().
		TORRENT_UNEXPORT aux::path_index_t add_file(error_code& ec,
			string_view filename,
			bool borrow,
			aux::path_index_t dir,
			std::int64_t file_size,
			file_flags_t file_flags = {},
			std::int64_t mtime = 0,
			std::int32_t root_hash_offset = no_root_hash);

		// internal
		// adds a symlink placeholder named ``filename`` directly under
		// directory ``dir``, same conventions as add_file() above.
		// Symlinks are always empty (0-byte) files. The symlink
		// self-points until a caller resolves it via
		// internal_set_symlink_target(); this is the deferred-placeholder
		// case used while parsing a real .torrent, before the rest of the
		// file list/tree has been parsed. Returns the new symlink's own
		// path_index_t.
		TORRENT_UNEXPORT aux::path_index_t add_symlink(error_code& ec,
			string_view filename,
			bool borrow,
			aux::path_index_t dir,
			file_flags_t file_flags,
			std::int64_t mtime);

		// internal
		// same as the overload above, but for the deprecated, low-level
		// string-based add_file() API (and tests) only: ``target``, if
		// non-empty, is the raw (not yet validated) target text, stored as
		// its own fresh path_element rather than resolved against the
		// tree, since it doesn't necessarily correspond to any file this
		// file_storage knows about.
		TORRENT_UNEXPORT aux::path_index_t add_symlink(error_code& ec,
			string_view filename,
			bool borrow,
			aux::path_index_t dir,
			file_flags_t file_flags,
			std::int64_t mtime,
			string_view target);

		// internal
		// sets the already-resolved symlink target for the file at
		// ``index`` (must have flag_symlink set) to path_element
		// ``target``.
		TORRENT_UNEXPORT void internal_set_symlink_target(
			file_index_t index, aux::path_index_t target);

#if TORRENT_ABI_VERSION < 4
		// internal
		// sets ``ec`` if ``new_filename`` has too many directory components
		TORRENT_UNEXPORT void rename_file_impl(
			file_index_t index, std::string const& new_filename, error_code& ec);
#endif

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
		// including pad files
		std::int64_t total_size() const { return m_total_size; }

		// returns the sum of all non pad file sizes
		std::int64_t size_on_disk() const { return m_size_on_disk; }

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

		// returns the number of blocks there are in the typical piece. There
		// may be fewer in the last piece)
		int blocks_per_piece() const;

		// set and get the name of this torrent. For multi-file torrents, this is also
		// the name of the root directory all the files are stored in.
		void set_name(std::string const& n) { m_name = n; }
		std::string const& name() const { return m_name; }

		// swap all content of *this* with *ti*.
		void swap(file_storage& ti) noexcept;

#if TORRENT_ABI_VERSION < 4
		TORRENT_DEPRECATED
		void canonicalize();

		// always returns a default constructed hash.
		TORRENT_DEPRECATED
		sha1_hash hash(file_index_t index) const;
#endif

		// These functions are used to query attributes of files at
		// a given index.
		//
		// ``root()`` returns the SHA-256 merkle tree root of the specified file,
		// in case this is a v2 torrent. Otherwise returns zeros.
		// ``root_ptr()`` returns a pointer to the SHA-256 merkle tree root hash
		// for the specified file. The pointer points into the buffer this
		// file_storage was constructed with (see the ``file_storage(char
		// const*)`` constructor), it is not owned by this object. Torrents
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
		// ``symlink()`` returns the path the file at ``index`` is a symlink
		// to. If the file is not a symlink, the returned string is empty.
		//
		// ``file_offset()`` returns the byte offset within the torrent file
		// where this file starts. It can be used to map the file to a piece
		// index (given the piece size).
		sha256_hash root(file_index_t index) const;
		char const* root_ptr(file_index_t index) const;
		std::string symlink(file_index_t index) const;
		std::time_t mtime(file_index_t index) const;
		std::string file_path(file_index_t index, std::string const& save_path = "") const;
		aux::file_name_view file_name(file_index_t index) const;
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

		// index of first piece node in the merkle tree. There's no need for
		// clients to call these.
		TORRENT_DEPRECATED int file_first_piece_node(file_index_t index) const;
		TORRENT_DEPRECATED int file_first_block_node(file_index_t index) const;

		// the file is a pad file. It's required to contain zeros
		// at it will not be saved to disk. Its purpose is to make
		// the following file start on a piece boundary.
		static inline constexpr file_flags_t flag_pad_file = 0_bit;

		// this file has the hidden attribute set. This is primarily
		// a windows attribute
		static inline constexpr file_flags_t flag_hidden = 1_bit;

		// this file has the executable attribute set.
		static inline constexpr file_flags_t flag_executable = 2_bit;

		// this file is a symbolic link. It should have a link
		// target string associated with it.
		static inline constexpr file_flags_t flag_symlink = 3_bit;

		// internal
		// per-path_element data computed by compute_element_hashes(): every
		// element's crc32 hash of its full path from the torrent root
		// (lower-case, no trailing separator), and whether the element is
		// used as a directory (is some other element's parent). Pad files
		// are never represented here, whether as the directory they
		// synthesize their own leaf name under or otherwise: they never
		// touch disk, so a naming collision involving one (or a directory
		// only ever referenced by one) is never a real conflict, see
		// resolve_duplicate_filenames(). The torrent's own root directory
		// (file_storage::name()) has no entry of its own either: a file
		// legitimately reconstructs to exactly that name when it has no
		// sub-directory components (e.g. single-file torrents), so it
		// isn't a real collision.
		struct element_hashes
		{
			aux::vector<std::uint32_t, aux::path_index_t> crc;
			aux::vector<bool, aux::path_index_t> is_dir;
		};

		// internal
		// computes element_hashes for this file_storage. A path_element's
		// parent always has a lower index than the element itself, so this
		// is a single forward pass, each element extending its parent's
		// already-computed hash rather than being rebuilt from the root.
		element_hashes compute_element_hashes() const;

		// internal
		// returns the crc32 hash of file_path(idx, ""), using an
		// element_hashes already computed by compute_element_hashes() for
		// this file_storage, instead of re-walking idx's whole path chain
		// from the root. idx must not be a pad file: a pad-file naming
		// collision is never a real conflict (see
		// resolve_duplicate_filenames()), so callers skip them rather than
		// hash them.
		std::uint32_t file_hash(element_hashes const& eh, file_index_t idx) const;

		// internal
		// hashes every path element (via compute_element_hashes()), then
		// returns that result if any non-pad file's hash (file_hash())
		// collides with a directory's hash or another file's hash;
		// std::nullopt if there's no collision at all. The full computation
		// always runs, even once a collision is found: its only caller,
		// resolve_duplicate_filenames(), needs every file's and directory's
		// hash to find and rename all conflicts, not just the first, so
		// stopping early would just move the same work into that caller.
		std::optional<element_hashes> has_duplicate_filenames() const;

		// internal
		// reconstructs the full path (rooted at file_storage::name()) of the
		// directory identified by ``index``, as produced by
		// compute_element_hashes().
		std::string internal_directory_path(aux::path_index_t index) const;

		// returns a bitmask of flags from file_flags_t that apply
		// to file at ``index``.
		file_flags_t file_flags(file_index_t index) const;

#if TORRENT_ABI_VERSION < 5
		// returns true if the file at the specified index has been renamed to
		// have an absolute path, i.e. is not anchored in the save path of the
		// torrent.
		TORRENT_DEPRECATED
		bool file_absolute_path(file_index_t index) const;
#endif

		// returns the index of the file at the given offset or piece in the torrent
		file_index_t file_index_at_offset(std::int64_t offset) const;
		file_index_t file_index_at_piece(piece_index_t piece) const;
		file_index_t last_file_index_at_piece(piece_index_t piece) const;

		// finds the file with the given root hash and returns its index
		// if there is no file with the root hash, file_index_t{-1} is returned
		file_index_t file_index_for_root(sha256_hash const& root_hash) const;

		// returns the first or last piece index of the given file in the torrent
		piece_index_t piece_index_at_file(file_index_t f) const;
		piece_index_t last_piece_index_at_file(file_index_t f) const;

#if TORRENT_USE_INVARIANT_CHECKS
		// internal
		bool owns_name(file_index_t const f) const
		{
			return m_path_elements[m_files[f].path_element_index].name_len
				== aux::path_element::name_is_owned;
		}
#endif

#if TORRENT_ABI_VERSION <= 2
		// low-level function. returns a pointer to the internal storage for
		// the filename. This string may not be 0-terminated! Always nullptr
		// for pad files, since they have no stored name.
		// the ``file_name_len()`` function returns the length of the filename.
		// prefer to use ``file_name()`` instead.
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

		// returns true if this torrent contains v2 metadata.
		bool v2() const { return m_v2; }

#if TORRENT_ABI_VERSION < 4
		// internal
		// reconstructs a symlink's target path; used by create_torrent
		std::string internal_symlink(file_index_t index) const;
#endif

		// internal
		void remove_tail_padding();

		// grants direct access to m_files, m_path_elements, m_name and
		// path_element_name(), so it can compare two file_storage objects'
		// paths without building either side's full path string
		friend bool aux::files_compatible(file_storage const& lhs, file_storage const& rhs);

	private:
#if TORRENT_ABI_VERSION < 4
		// internal
		void canonicalize_impl(bool backwards_compatible);
#endif

		// backwards-compatibility wrapper for add_file(): resolves a joined
		// path string into an owned path_element chain, then calls
		// add_file_impl().
		void add_file_borrow_impl(error_code& ec,
			string_view filename,
			std::string const& path,
			std::int64_t file_size,
			file_flags_t file_flags,
			std::int64_t mtime,
			string_view symlink_path,
			std::int32_t root_hash_offset);

		// the one primitive that actually inserts a file_entry. Used
		// directly by the path_index_t-based add_file() overload, and
		// indirectly (via add_file_borrow_impl() and
		// resolve_owned_directory()) by the deprecated, joined-path-string
		// add_file() overloads.
		aux::path_index_t add_file_impl(error_code& ec,
			string_view filename,
			bool filename_borrow,
			aux::path_index_t dir,
			std::int64_t file_size,
			file_flags_t file_flags,
			std::int64_t mtime,
			std::int32_t root_hash_offset);

		// records ``mtime`` for the most recently added file (last_file()),
		// growing m_mtime on demand. A no-op when mtime is 0 (the common
		// case; m_mtime then stays empty entirely).
		void set_last_file_mtime(std::int64_t mtime);

		// converts a pointer into m_info_section into an offset relative to
		// m_info_section, or no_root_hash if root_hash is nullptr
		std::int32_t root_hash_to_offset(char const* root_hash) const;

		file_index_t last_file() const noexcept;

		// resolves the stored text of a path_element
		string_view path_element_name(aux::path_element const& e) const;

		// walks ``leaf``'s parent chain (root to leaf) and appends each
		// component's text, separator-joined, to ``out``. Iterative, since
		// the chain depth is derived from an untrusted .torrent file.
		// Returns the terminal sentinel the chain is rooted at (torrent_root,
		// path_is_absolute, pad_directory, or no_root_dir).
		aux::path_index_t reconstruct_path(aux::path_index_t leaf, std::string& out) const;

		// compares the path_element chain rooted at ``li`` against the one
		// rooted at ``ri`` in ``rhs``, leaf to root, without building
		// either side's full path string (as reconstruct_path() would).
		// Doesn't compare name(), even though a chain rooted at
		// torrent_root implies it's prepended: files_compatible(), the
		// only caller, already guarantees lhs and rhs share a name(). Used
		// for both a file's own path (path_element_index) and, for
		// symlinks, its target (symlink_element_index).
		bool path_chain_equal(
			aux::path_index_t li, file_storage const& rhs, aux::path_index_t ri) const;

#if TORRENT_ABI_VERSION == 1
		// recovers a file_entry's index from its address in m_files. Used by
		// the deprecated ABI-1 accessors that take a file_entry reference.
		file_index_t index_of(aux::file_entry const& fe) const;
#endif

		// the number of bytes in a regular piece
		// (i.e. not the potentially truncated last piece)
		int m_piece_length = 0;

		// the number of pieces in the torrent
		int m_num_pieces = 0;

		// whether this is a v2 torrent or not. Additional requirements apply to
		// v2 torrents
		bool m_v2 = false;

		// splits a definitely-relative ``path`` into a chain of owned
		// directory path_elements (not including path's own last
		// component), rooted at torrent_root if the chain matches name(), or
		// at the no_root_dir sentinel otherwise. Returns the terminal
		// directory's index (compare against aux::path_element::no_root_dir
		// to tell which of those two applied), and path's own last
		// component in ``leaf_out``. Sets ``ec`` and returns {} if ``path``
		// has too many directory components.
		aux::path_index_t resolve_owned_directory(
			std::string const& path, string_view& leaf_out, error_code& ec);

		// the list of files that this torrent consists of
		aux::vector<aux::file_entry, file_index_t> m_files;

		// the number of files in m_files that are symlinks; used to detect
		// "all files added so far have been symlinks" for v1/v2 ambiguity
		// purposes.
		int m_num_symlinks = 0;

		// the modification times of each file. This vector
		// is empty if no file have a modification time.
		// each element corresponds to the file with the same
		// index in m_files
		aux::vector<std::time_t, file_index_t> m_mtime;

		// the tree of path components (directories and filenames) files
		// reference. aux::file_entry::path_element_index points into this
		// array; walking aux::path_element::parent links reconstructs a
		// file's full path (see reconstruct_path()).
		aux::vector<aux::path_element, aux::path_index_t> m_path_elements;

		// name of torrent. For multi-file torrents
		// this is always the root directory
		std::string m_name;

		// the sum of all file sizes
		std::int64_t m_total_size = 0;

		// the sum of all non-pad file sizes
		std::int64_t m_size_on_disk = 0;

		// the buffer that aux::file_entry::root_offset values are relative
		// to. Borrowed, not owned by this object. This is the same pointer as
		// torrent_info::m_info_section, when this file_storage was populated
		// while parsing a .torrent file.
		char const* m_info_section = nullptr;
};

TORRENT_VERSION_NAMESPACE_4_END

namespace aux {
	struct rename_entry
	{
		enum mode_t {
			// the string is the full path, to be appended to the torrent name
			full_path,
			// the string is a full path relative to the save path, not the torrent name
			no_root_path,
			// the string is an absolute path
			absolute_path
		};

		mode_t mode;
		std::string path;
	};
}

	// this is a wrapper around file_storage for renaming files without having
	// to mutate the file_storage object itself.
	struct TORRENT_EXPORT renamed_files
	{
		// returns information about the file at ``index`` in ``fs``,
		// honoring any rename recorded in this object.
		// ``file_path()`` returns the full on-disk path (prepending
		// ``save_path`` to relative paths). ``file_name()`` returns
		// just the leaf filename. ``file_absolute_path()`` returns
		// true if the recorded rename is an absolute path (in which
		// case ``save_path`` is ignored).
		std::string file_path(file_storage const& fs, file_index_t index, std::string const& save_path = "") const;
		aux::file_name_view file_name(file_storage const& fs, file_index_t index) const;
		bool file_absolute_path(file_storage const& fs, file_index_t index) const;

		// records that the file at ``index`` in ``fs`` should be
		// renamed to ``new_filename`` on disk. The underlying
		// ``file_storage`` is not modified.
		void rename_file(file_storage const& fs, file_index_t index, std::string const& new_filename);

		// bulk import or export the set of file renames recorded in
		// this object. ``import_filenames()`` adds the given renames
		// (dropping entries whose file index is out of range).
		// ``export_filenames()`` returns the current renames as a map
		// suitable for serializing and later passing back to
		// ``import_filenames()``.
		void import_filenames(file_storage const& fs, std::map<file_index_t, std::string> const& renamed_files);
		std::map<file_index_t, std::string> export_filenames(file_storage const& fs) const;
	private:
		std::unordered_map<file_index_t, aux::rename_entry> m_renamed_files;
	};

	// a lightweight view that pairs a ``file_storage`` with the rename
	// overrides in a ``renamed_files``. It exposes the read-only subset
	// of ``file_storage`` that downstream code (e.g. the disk I/O
	// backends) needs, with file paths automatically resolved through
	// the rename layer.
	struct TORRENT_EXPORT filenames
	{
		// construct a view over ``fs`` with the renames recorded in
		// ``rf`` applied. Both references must outlive the view.
		filenames(file_storage const& fs, renamed_files const& rf)
			: m_files(fs)
			, m_renames(rf)
		{}

		// returns the file flags for the file at ``index``
		file_flags_t file_flags(file_index_t const index) const
		{ return m_files.file_flags(index); }

		// one past-the-end file index
		file_index_t end_file() const noexcept { return m_files.end_file(); }

		// returns true if the file at ``index`` is a pad file
		bool pad_file_at(file_index_t const index) const { return m_files.pad_file_at(index); }

		// returns the file size or file offset for the file at ``index``. The
		// file offset is in the torrent space, of all files in the torrent, in
		// the order they are defined.
		std::int64_t file_size(file_index_t const index) const
		{
			return m_files.file_size(index);
		}
		std::int64_t file_offset(file_index_t const index) const
		{
			return m_files.file_offset(index);
		}

		// returns the on-disk path for the file at ``index``, applying
		// any rename recorded in the underlying ``renamed_files``. If
		// ``save_path`` is non-empty it is prepended to relative
		// paths. ``file_absolute_path()`` returns true if the recorded
		// rename is an absolute path (in which case ``save_path`` is
		// ignored).
		std::string file_path(file_index_t const index, std::string const& save_path = "") const
		{
			return m_renames.file_path(m_files, index, save_path);
		}
		bool file_absolute_path(file_index_t const index) const
		{
			return m_renames.file_absolute_path(m_files, index);
		}

		// If the file at ``index`` is a symbolic link, its link target is returned.
		// otherwise an empty string.
		std::string symlink(file_index_t const index) const { return m_files.symlink(index); }

		// all file indices in the underlying file_storage. Convenient in
		// range-for loops.
		index_range<file_index_t> file_range() const noexcept
		{
			return m_files.file_range();
		}

		// the number of files in the underlying file_storage, the
		// number of pieces, and the one-past-the-end piece index.
		int num_files() const noexcept { return m_files.num_files(); }
		piece_index_t end_piece() const
		{
			return m_files.end_piece();
		}
		int num_pieces() const { return m_files.num_pieces(); }

		// returns the list of file slices that make up the piece at
		// ``piece``, starting at ``offset`` into the piece and
		// covering ``size`` bytes. Each entry identifies one file and
		// the byte range within it that contributes to the requested
		// block.
		std::vector<file_slice> map_block(piece_index_t const piece, std::int64_t const offset
			, std::int64_t const size) const
		{
			return m_files.map_block(piece, offset, size);
		}

		// inverse of ``map_block()``: maps a byte range within the
		// file at ``file`` (starting at ``offset``, of length
		// ``size``) to a ``peer_request`` identifying the piece,
		// piece-offset and length in torrent space.
		peer_request map_file(file_index_t const file, std::int64_t const offset, int const size) const
		{
			return m_files.map_file(file, offset, size);
		}

		// returns the v2 merkle root (``pieces root``) for the file
		// at ``index``. Returns a zero hash for v1-only torrents.
		sha256_hash root(file_index_t const index) const
		{
			return m_files.root(index);
		}

		// the piece size, in bytes. All pieces have this size except
		// the last, which may be shorter.
		int piece_length() const
		{
			return m_files.piece_length();
		}

	private:
		file_storage const& m_files;
		renamed_files const& m_renames;
	};

namespace aux {

	TORRENT_EXTRA_EXPORT
	int calc_num_pieces(file_storage const& fs);

	// this is used when loading v2 torrents that are backwards compatible with
	// v1 torrents. Both v1 and v2 structures must describe the same file layout,
	// this compares the two.
	TORRENT_EXTRA_EXPORT
	bool files_compatible(file_storage const& lhs, file_storage const& rhs);

	// returns the piece range that entirely falls within the specified file. the
	// end piece is one-past the last piece that entirely falls within the file.
	// i.e. the range can conveniently be iterated over. No edge partial pieces
	// will be included.
	// these are templated on the file-layout type so they accept both a
	// file_storage and a filenames view (which exposes the same map_file(),
	// file_size(), piece_length(), num_files() and num_pieces() members). the
	// rename layer in filenames only affects file paths, not the piece layout,
	// so the result is identical for either. The instantiations are defined in
	// file_storage.cpp.
	template <typename FileStorage>
	TORRENT_EXTRA_EXPORT index_range<piece_index_t> file_piece_range_exclusive(
		FileStorage const& fs, file_index_t file);

	// returns the piece range of pieces that overlaps with the specified file.
	// the end piece is one-past the last piece. i.e. the range can conveniently
	// be iterated over.
	template <typename FileStorage>
	TORRENT_EXTRA_EXPORT index_range<piece_index_t> file_piece_range_inclusive(
		FileStorage const& fs, file_index_t file);

	// only the instantiations explicitly defined in file_storage.cpp are used
	extern template index_range<piece_index_t> file_piece_range_exclusive(
		file_storage const&, file_index_t);
	extern template index_range<piece_index_t> file_piece_range_inclusive(
		file_storage const&, file_index_t);
	extern template index_range<piece_index_t> file_piece_range_inclusive(
		filenames const&, file_index_t);
} // namespace aux
} // namespace libtorrent

#endif // TORRENT_FILE_STORAGE_HPP_INCLUDED
