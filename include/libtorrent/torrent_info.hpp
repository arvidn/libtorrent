/*

Copyright (c) 2003-2011, 2013-2020, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2016-2018, 2020, Alden Torres
Copyright (c) 2016, Markus
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2017-2019, Steven Siloti
Copyright (c) 2017, 2019, Andrei Kurushin
Copyright (c) 2019, Amir Abrams
Copyright (c) 2020, Mike Tzou
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

#ifndef TORRENT_TORRENT_INFO_HPP_INCLUDED
#define TORRENT_TORRENT_INFO_HPP_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/shared_array.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/copy_ptr.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/info_hash.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"

namespace libtorrent {

	struct invariant_access;

namespace aux {

	// internal, exposed for the unit test
	TORRENT_EXTRA_EXPORT void sanitize_append_path_element(std::string& path
		, string_view element);
	TORRENT_EXTRA_EXPORT bool verify_encoding(std::string& target);
}

	// the web_seed_entry holds information about a web seed (also known
	// as URL seed or HTTP seed). It is essentially a URL with some state
	// associated with it. For more information, see `BEP 17`_ and `BEP 19`_.
	struct TORRENT_EXPORT web_seed_entry
	{
		// http seeds are different from url seeds in the
		// protocol they use. http seeds follows the original
		// http seed spec. by John Hoffman
		enum type_t { url_seed, http_seed };

		using headers_t = std::vector<std::pair<std::string, std::string>>;

		// hidden
		web_seed_entry(std::string url_, type_t type_
			, std::string auth_ = std::string()
			, headers_t extra_headers_ = headers_t());

		// URL and type comparison
		bool operator==(web_seed_entry const& e) const
		{ return type == e.type && url == e.url; }

		// URL and type less-than comparison
		bool operator<(web_seed_entry const& e) const
		{
			if (url < e.url) return true;
			if (url > e.url) return false;
			return type < e.type;
		}

		// The URL of the web seed
		std::string url;

		// Optional authentication. If this is set, it's passed
		// in as HTTP basic auth to the web seed. The format is:
		// username:password.
		std::string auth;

		// Any extra HTTP headers that need to be passed to the web seed
		headers_t extra_headers;

		// The type of web seed (see type_t)
		std::uint8_t type;
	};

	// hidden
	class from_span_t {};

	// used to disambiguate a bencoded buffer and a filename
	extern TORRENT_EXPORT from_span_t from_span;

	// this object holds configuration options for limits to use when loading
	// torrents. They are meant to prevent loading potentially malicious torrents
	// that cause excessive memory allocations.
	struct TORRENT_EXPORT load_torrent_limits
	{
		// the max size of a .torrent file to load into RAM
		int max_buffer_size = 10000000;

		// the max number of pieces allowed in the torrent
		int max_pieces = 0x200000;

		// the max recursion depth in the bdecoded structure
		int max_decode_depth = 100;

		// the max number of bdecode tokens
		int max_decode_tokens = 3000000;
	};

	using torrent_info_flags_t = flags::bitfield_flag<std::uint8_t, struct torrent_info_flags_tag>;

TORRENT_VERSION_NAMESPACE_3

	// the torrent_info class holds the information found in a .torrent file.
	class TORRENT_EXPORT torrent_info
	{
	public:

		// The constructor that takes an info-hash  will initialize the info-hash
		// to the given value, but leave all other fields empty. This is used
		// internally when downloading torrents without the metadata. The
		// metadata will be created by libtorrent as soon as it has been
		// downloaded from the swarm.
		//
		// The constructor that takes a bdecode_node will create a torrent_info
		// object from the information found in the given torrent_file. The
		// bdecode_node represents a tree node in an bencoded file. To load an
		// ordinary .torrent file into a bdecode_node, use bdecode().
		//
		// The version that takes a buffer pointer and a size will decode it as a
		// .torrent file and initialize the torrent_info object for you.
		//
		// The version that takes a filename will simply load the torrent file
		// and decode it inside the constructor, for convenience. This might not
		// be the most suitable for applications that want to be able to report
		// detailed errors on what might go wrong.
		//
		// There is an upper limit on the size of the torrent file that will be
		// loaded by the overload taking a filename. If it's important that even
		// very large torrent files are loaded, use one of the other overloads.
		//
		// The overloads that takes an ``error_code const&`` never throws if an
		// error occur, they will simply set the error code to describe what went
		// wrong and not fully initialize the torrent_info object. The overloads
		// that do not take the extra error_code parameter will always throw if
		// an error occurs. These overloads are not available when building
		// without exception support.
		//
		// The overload that takes a ``span`` also needs an extra parameter of
		// type ``from_span_t`` to disambiguate the ``std::string`` overload for
		// string literals. There is an object in the libtorrent namespace of this
		// type called ``from_span``.
#ifndef BOOST_NO_EXCEPTIONS
		explicit torrent_info(bdecode_node const& torrent_file);
		torrent_info(char const* buffer, int size)
			: torrent_info(span<char const>{buffer, size}, from_span) {}
		explicit torrent_info(span<char const> buffer, from_span_t);
		explicit torrent_info(std::string const& filename);
		torrent_info(std::string const& filename, load_torrent_limits const& cfg);
		torrent_info(span<char const> buffer, load_torrent_limits const& cfg, from_span_t);
		torrent_info(bdecode_node const& torrent_file, load_torrent_limits const& cfg);
#endif // BOOST_NO_EXCEPTIONS
		torrent_info(torrent_info const& t);
		explicit torrent_info(info_hash_t const& info_hash);
		torrent_info(bdecode_node const& torrent_file, error_code& ec);
		torrent_info(char const* buffer, int size, error_code& ec)
			: torrent_info(span<char const>{buffer, size}, ec, from_span) {}
		torrent_info(span<char const> buffer, error_code& ec, from_span_t);
		torrent_info(std::string const& filename, error_code& ec);

#if TORRENT_ABI_VERSION == 1
#ifndef BOOST_NO_EXCEPTIONS
		TORRENT_DEPRECATED
		torrent_info(char const* buffer, int size, int)
			: torrent_info(span<char const>{buffer, size}, from_span) {}
#endif
		TORRENT_DEPRECATED
		torrent_info(bdecode_node const& torrent_file, error_code& ec, int)
			: torrent_info(torrent_file, ec) {}
		TORRENT_DEPRECATED
		torrent_info(std::string const& filename, error_code& ec, int)
			: torrent_info(filename, ec) {}
		TORRENT_DEPRECATED
		torrent_info(char const* buffer, int size, error_code& ec, int)
			: torrent_info(span<char const>{buffer, size}, ec, from_span) {}
#endif // TORRENT_ABI_VERSION

		// frees all storage associated with this torrent_info object
		~torrent_info();

		// hidden
		torrent_info& operator=(torrent_info const&) = delete;
		torrent_info& operator=(torrent_info&&);

		// The file_storage object contains the information on how to map the
		// pieces to files. It is separated from the torrent_info object because
		// when creating torrents a storage object needs to be created without
		// having a torrent file. When renaming files in a storage, the storage
		// needs to make its own copy of the file_storage in order to make its
		// mapping differ from the one in the torrent file.
		//
		// ``orig_files()`` returns the original (unmodified) file storage for
		// this torrent. This is used by the web server connection, which needs
		// to request files with the original names. Filename may be changed using
		// ``torrent_info::rename_file()``.
		//
		// For more information on the file_storage object, see the separate
		// document on how to create torrents.
		file_storage const& files() const { return m_files; }
		file_storage const& orig_files() const;

		// Renames the file with the specified index to the new name. The new
		// filename is reflected by the ``file_storage`` returned by ``files()``
		// but not by the one returned by ``orig_files()``.
		//
		// If you want to rename the base name of the torrent (for a multi file
		// torrent), you can copy the ``file_storage`` (see files() and
		// orig_files() ), change the name, and then use `remap_files()`_.
		//
		// The ``new_filename`` can both be a relative path, in which case the
		// file name is relative to the ``save_path`` of the torrent. If the
		// ``new_filename`` is an absolute path (i.e. ``is_complete(new_filename)
		// == true``), then the file is detached from the ``save_path`` of the
		// torrent. In this case the file is not moved when move_storage() is
		// invoked.
		void rename_file(file_index_t index, std::string const& new_filename);

		// .. warning::
		// 	Using `remap_files()` is discouraged as it's incompatible with v2
		// 	torrents. This is because the piece boundaries and piece hashes in
		// 	v2 torrents are intimately tied to the file boundaries. Instead,
		// 	just rename individual files, or implement a custom disk_interface
		// 	to customize how to store files.
		//
		// Remaps the file storage to a new file layout. This can be used to, for
		// instance, download all data in a torrent to a single file, or to a
		// number of fixed size sector aligned files, regardless of the number
		// and sizes of the files in the torrent.
		//
		// The new specified ``file_storage`` must have the exact same size as
		// the current one.
		void remap_files(file_storage const& f);

		// ``add_tracker()`` adds a tracker to the announce-list. The ``tier``
		// determines the order in which the trackers are to be tried.
		// The ``trackers()`` function will return a sorted vector of
		// announce_entry. Each announce entry contains a string, which is
		// the tracker url, and a tier index. The tier index is the high-level
		// priority. No matter which trackers that works or not, the ones with
		// lower tier will always be tried before the one with higher tier
		// number. For more information, see announce_entry.
		//
		// ``trackers()`` returns all entries from announce-list.
		//
		// ``clear_trackers()`` removes all trackers from announce-list.
		void add_tracker(std::string const& url, int tier = 0);
		void add_tracker(std::string const& url, int tier
			, announce_entry::tracker_source source);
		std::vector<announce_entry> const& trackers() const { return m_urls; }
		void clear_trackers();

		// These two functions are related to `BEP 38`_ (mutable torrents). The
		// vectors returned from these correspond to the "similar" and
		// "collections" keys in the .torrent file. Both info-hashes and
		// collections from within the info-dict and from outside of it are
		// included.
		std::vector<sha1_hash> similar_torrents() const;
		std::vector<std::string> collections() const;

#if TORRENT_ABI_VERSION == 1
		// deprecated in 0.16. Use web_seeds() instead
		TORRENT_DEPRECATED
		std::vector<std::string> url_seeds() const;
		TORRENT_DEPRECATED
		std::vector<std::string> http_seeds() const;
#endif // TORRENT_ABI_VERSION

		// ``web_seeds()`` returns all url seeds and http seeds in the torrent.
		// Each entry is a ``web_seed_entry`` and may refer to either a url seed
		// or http seed.
		//
		// ``add_url_seed()`` and ``add_http_seed()`` adds one url to the list of
		// url/http seeds.
		//
		// ``set_web_seeds()`` replaces all web seeds with the ones specified in
		// the ``seeds`` vector.
		//
		// The ``extern_auth`` argument can be used for other authorization
		// schemes than basic HTTP authorization. If set, it will override any
		// username and password found in the URL itself. The string will be sent
		// as the HTTP authorization header's value (without specifying "Basic").
		//
		// The ``extra_headers`` argument defaults to an empty list, but can be
		// used to insert custom HTTP headers in the requests to a specific web
		// seed.
		//
		// See http-seeding_ for more information.
		void add_url_seed(std::string const& url
			, std::string const& ext_auth = std::string()
			, web_seed_entry::headers_t const& ext_headers = web_seed_entry::headers_t());
		void add_http_seed(std::string const& url
			, std::string const& extern_auth = std::string()
			, web_seed_entry::headers_t const& extra_headers = web_seed_entry::headers_t());
		std::vector<web_seed_entry> const& web_seeds() const { return m_web_seeds; }
		void set_web_seeds(std::vector<web_seed_entry> seeds);

		// ``total_size()`` returns the total number of bytes the torrent-file
		// represents. Note that this is the number of pieces times the piece
		// size (modulo the last piece possibly being smaller). With pad files,
		// the total size will be larger than the sum of all (regular) file
		// sizes.
		std::int64_t total_size() const { return m_files.total_size(); }

		// ``piece_length()`` and ``num_pieces()`` returns the number of byte
		// for each piece and the total number of pieces, respectively. The
		// difference between ``piece_size()`` and ``piece_length()`` is that
		// ``piece_size()`` takes the piece index as argument and gives you the
		// exact size of that piece. It will always be the same as
		// ``piece_length()`` except in the case of the last piece, which may be
		// smaller.
		int piece_length() const { return m_files.piece_length(); }
		int num_pieces() const { return m_files.num_pieces(); }

		// ``last_piece()`` returns the index to the last piece in the torrent and
		// ``end_piece()`` returns the index to the one-past-end piece in the
		// torrent
		// ``piece_range()`` returns an implementation-defined type that can be
		// used as the container in a range-for loop. Where the values are the
		// indices of all pieces in the file_storage.
		piece_index_t last_piece() const { return m_files.last_piece(); }
		piece_index_t end_piece() const
		{
			TORRENT_ASSERT(m_files.num_pieces() > 0);
			return m_files.end_piece();
		}
		index_range<piece_index_t> piece_range() const
		{ return m_files.piece_range(); }

		// returns the info-hash of the torrent. For BitTorrent v2 support, use
		// ``info_hashes()`` to get an object that may hold both a v1 and v2
		// info-hash
		sha1_hash info_hash() const noexcept;
		info_hash_t const& info_hashes() const { return m_info_hash; }

		// returns whether this torrent has v1 and/or v2 metadata, respectively.
		// Hybrid torrents have both. These are shortcuts for
		// info_hashes().has_v1() and info_hashes().has_v2() calls.
		bool v1() const;
		bool v2() const;

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.0. Use the variants that take an index instead
		// internal_file_entry is no longer exposed in the API
		using file_iterator = file_storage::iterator;
		using reverse_file_iterator = file_storage::reverse_iterator;

		// This class will need some explanation. First of all, to get a list of
		// all files in the torrent, you can use ``begin_files()``,
		// ``end_files()``, ``rbegin_files()`` and ``rend_files()``. These will
		// give you standard vector iterators with the type
		// ``internal_file_entry``, which is an internal type.
		//
		// You can resolve it into the public representation of a file
		// (``file_entry``) using the ``file_storage::at`` function, which takes
		// an index and an iterator.
		TORRENT_DEPRECATED
		file_iterator begin_files() const { return m_files.begin_deprecated(); }
		TORRENT_DEPRECATED
		file_iterator end_files() const { return m_files.end_deprecated(); }
		reverse_file_iterator rbegin_files() const { return m_files.rbegin_deprecated(); }
		TORRENT_DEPRECATED
		reverse_file_iterator rend_files() const { return m_files.rend_deprecated(); }

		TORRENT_DEPRECATED
		file_iterator file_at_offset(std::int64_t offset) const
		{ return m_files.file_at_offset_deprecated(offset); }

#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

		TORRENT_DEPRECATED
		file_entry file_at(int index) const { return m_files.at_deprecated(index); }

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif // TORRENT_ABI_VERSION

		// If you need index-access to files you can use the ``num_files()`` along
		// with the ``file_path()``, ``file_size()``-family of functions to access
		// files using indices.
		int num_files() const { return m_files.num_files(); }

		// This function will map a piece index, a byte offset within that piece
		// and a size (in bytes) into the corresponding files with offsets where
		// that data for that piece is supposed to be stored. See file_slice.
		std::vector<file_slice> map_block(piece_index_t const piece
			, std::int64_t offset, int size) const
		{
			TORRENT_ASSERT(is_loaded());
			return m_files.map_block(piece, offset, size);
		}

		// This function will map a range in a specific file into a range in the
		// torrent. The ``file_offset`` parameter is the offset in the file,
		// given in bytes, where 0 is the start of the file. See peer_request.
		//
		// The input range is assumed to be valid within the torrent.
		// ``file_offset`` + ``size`` is not allowed to be greater than the file
		// size. ``file_index`` must refer to a valid file, i.e. it cannot be >=
		// ``num_files()``.
		peer_request map_file(file_index_t const file, std::int64_t offset, int size) const
		{
			TORRENT_ASSERT(is_loaded());
			return m_files.map_file(file, offset, size);
		}

#if TORRENT_ABI_VERSION == 1
// ------- start deprecation -------
		// deprecated in 1.2
		void load(char const*, int, error_code&) {}
		void unload() {}

		TORRENT_DEPRECATED
		explicit torrent_info(entry const& torrent_file);
// ------- end deprecation -------
#endif

		// Returns the SSL root certificate for the torrent, if it is an SSL
		// torrent. Otherwise returns an empty string. The certificate is
		// the public certificate in x509 format.
		string_view ssl_cert() const;

		// returns true if this torrent_info object has a torrent loaded.
		// This is primarily used to determine if a magnet link has had its
		// metadata resolved yet or not.
		bool is_valid() const { return m_files.is_valid(); }

		// returns true if this torrent is private. i.e., the client should not
		// advertise itself on the trackerless network (the Kademlia DHT) for this torrent.
		bool priv() const { return bool(m_flags & private_torrent); }

		// returns true if this is an i2p torrent. This is determined by whether
		// or not it has a tracker whose URL domain name ends with ".i2p". i2p
		// torrents disable the DHT and local peer discovery as well as talking
		// to peers over anything other than the i2p network.
		bool is_i2p() const { return bool(m_flags & i2p); }

		// internal
		bool v2_piece_hashes_verified() const { return bool(m_flags & v2_has_piece_hashes); }
		void set_piece_layers(aux::vector<aux::vector<char>, file_index_t> pl);

		// returns the piece size of file with ``index``. This will be the same as piece_length(),
		// except for the last piece, which may be shorter.
		int piece_size(piece_index_t index) const { return m_files.piece_size(index); }

		// ``hash_for_piece()`` takes a piece-index and returns the 20-bytes
		// sha1-hash for that piece and ``info_hash()`` returns the 20-bytes
		// sha1-hash for the info-section of the torrent file.
		// ``hash_for_piece_ptr()`` returns a pointer to the 20 byte sha1 digest
		// for the piece. Note that the string is not 0-terminated.
		sha1_hash hash_for_piece(piece_index_t index) const;
		char const* hash_for_piece_ptr(piece_index_t const index) const
		{
			TORRENT_ASSERT_PRECOND(index >= piece_index_t(0));
			TORRENT_ASSERT_PRECOND(index < m_files.end_piece());
			TORRENT_ASSERT(is_loaded());
			int const idx = static_cast<int>(index);
			TORRENT_ASSERT(m_piece_hashes > 0);
			TORRENT_ASSERT(m_piece_hashes < m_info_section_size);
			TORRENT_ASSERT(idx < int((m_info_section_size - m_piece_hashes) / 20));
			return &m_info_section[std::ptrdiff_t(m_piece_hashes) + idx * 20];
		}

		bool is_loaded() const { return m_files.num_files() > 0; }

#if TORRENT_ABI_VERSION <= 2
		// support for BEP 30 merkle torrents has been removed

		// ``merkle_tree()`` returns a reference to the merkle tree for this
		// torrent, if any.
		// ``set_merkle_tree()`` moves the passed in merkle tree into the
		// torrent_info object. i.e. ``h`` will not be identical after the call.
		// You need to set the merkle tree for a torrent that you've just created
		// (as a merkle torrent). The merkle tree is retrieved from the
		// ``create_torrent::merkle_tree()`` function, and need to be saved
		// separately from the torrent file itself. Once it's added to
		// libtorrent, the merkle tree will be persisted in the resume data.
		TORRENT_DEPRECATED
		std::vector<sha1_hash> const& merkle_tree() const { return m_merkle_tree; }
		TORRENT_DEPRECATED
		void set_merkle_tree(std::vector<sha1_hash>& h)
		{ TORRENT_ASSERT(h.size() == m_merkle_tree.size() ); m_merkle_tree.swap(h); }
#endif

		// ``name()`` returns the name of the torrent.
		// name contains UTF-8 encoded string.
		const std::string& name() const { return m_files.name(); }

		// ``creation_date()`` returns the creation date of the torrent as time_t
		// (`posix time`_). If there's no time stamp in the torrent file, 0 is
		// returned.
		// .. _`posix time`: http://www.opengroup.org/onlinepubs/009695399/functions/time.html
		std::time_t creation_date() const
		{ return m_creation_date; }

		// ``creator()`` returns the creator string in the torrent. If there is
		// no creator string it will return an empty string.
		const std::string& creator() const
		{ return m_created_by; }

		// ``comment()`` returns the comment associated with the torrent. If
		// there's no comment, it will return an empty string.
		// comment contains UTF-8 encoded string.
		const std::string& comment() const
		{ return m_comment; }

		// If this torrent contains any DHT nodes, they are put in this vector in
		// their original form (host name and port number).
		std::vector<std::pair<std::string, int>> const& nodes() const
		{ return m_nodes; }

		// This is used when creating torrent. Use this to add a known DHT node.
		// It may be used, by the client, to bootstrap into the DHT network.
		void add_node(std::pair<std::string, int> const& node)
		{ m_nodes.push_back(node); }

		// populates the torrent_info by providing just the info-dict buffer.
		// This is used when loading a torrent from a magnet link for instance,
		// where we only have the info-dict. The bdecode_node ``e`` points to a
		// parsed info-dictionary. ``ec`` returns an error code if something
		// fails (typically if the info dictionary is malformed).
		// The `max_pieces` parameter allows limiting the amount of memory
		// dedicated to loading the torrent, and fails for torrents that exceed
		// the limit. To load large torrents, this limit may also need to be
		// raised in settings_pack::max_piece_count and in calls to
		// read_resume_data().
		bool parse_info_section(bdecode_node const& info, error_code& ec, int max_pieces);

#if TORRENT_ABI_VERSION < 3
		TORRENT_DEPRECATED
		bool parse_info_section(bdecode_node const& info, error_code& ec);
#endif

		// This function looks up keys from the info-dictionary of the loaded
		// torrent file. It can be used to access extension values put in the
		// .torrent file. If the specified key cannot be found, it returns nullptr.
		bdecode_node info(char const* key) const;

		// returns a the raw info section of the torrent file.
		// The underlying buffer is still owned by the torrent_info object
		span<char const> info_section() const
		{ return span<char const>(m_info_section.get(), m_info_section_size); }

#if TORRENT_ABI_VERSION <= 2
		// swap the content of this and ``ti``.
		TORRENT_DEPRECATED
		void swap(torrent_info& ti);

		// ``metadata()`` returns a the raw info section of the torrent file. The size
		// of the metadata is returned by ``metadata_size()``.
		// Even though the bytes returned by ``metadata()`` are not ``const``,
		// they must not be modified.
		TORRENT_DEPRECATED
		int metadata_size() const { return m_info_section_size; }
		TORRENT_DEPRECATED
		boost::shared_array<char> metadata() const;
#endif

		// return the bytes of the piece layer hashes for the specified file. If
		// the file doesn't have a piece layer, an empty span is returned.
		// The span size is divisible by 32, the size of a SHA-256 hash.
		// If the size of the file is smaller than or equal to the piece size,
		// the files "root hash" is the hash of the file and is not saved
		// separately in the "piece layers" field, but this function still
		// returns the root hash of the file in that case.
		span<char const> piece_layer(file_index_t) const;

		// clears the piece layers from the torrent_info. This is done by the
		// session when a torrent is added, to avoid storing it twice. The piece
		// layer (or other hashes part of the merkle tree) are stored in the
		// internal torrent object.
		void free_piece_layers();

		// internal
		void internal_set_creator(string_view);
		void internal_set_creation_date(std::time_t);
		void internal_set_comment(string_view);

#if TORRENT_ABI_VERSION <= 2
		// support for BEP 30 merkle torrents has been removed

		// internal
		TORRENT_DEPRECATED
		bool add_merkle_nodes(std::map<int, sha1_hash> const&
			, piece_index_t) { return false; }
		TORRENT_DEPRECATED
		std::map<int, sha1_hash> build_merkle_list(piece_index_t) const
		{
			return std::map<int, sha1_hash>();
		}

		// returns whether or not this is a merkle torrent.
		// see `BEP 30`__.
		//
		// __ https://www.bittorrent.org/beps/bep_0030.html
		TORRENT_DEPRECATED
		bool is_merkle_torrent() const { return !m_merkle_tree.empty(); }
#endif

	private:

		// populate the piece layers from the metadata
		bool parse_piece_layers(bdecode_node const& e, error_code& ec);

		bool parse_torrent_file(bdecode_node const& torrent_file, error_code& ec, int piece_limit);

		void resolve_duplicate_filenames();

		// the slow path, in case we detect/suspect a name collision
		void resolve_duplicate_filenames_slow();

#if TORRENT_USE_INVARIANT_CHECKS
		friend struct ::lt::invariant_access;
		void check_invariant() const;
#endif

		void copy_on_write();

		file_storage m_files;

		// if m_files is modified, it is first copied into
		// m_orig_files so that the original name and
		// filenames are preserved.
		// the original filenames are required to build URLs for web seeds for
		// instance
		copy_ptr<const file_storage> m_orig_files;

		// the URLs to the trackers
		aux::vector<announce_entry> m_urls;
		std::vector<web_seed_entry> m_web_seeds;
		// dht nodes to add to the routing table/bootstrap from
		std::vector<std::pair<std::string, int>> m_nodes;

		// the info-hashes (20 bytes each) in the "similar" key. These are offsets
		// into the info dict buffer.
		std::vector<std::int32_t> m_similar_torrents;

		// these are similar torrents from outside of the info-dict. We can't
		// have non-owning pointers to those, as we only keep the info-dict
		// around.
		std::vector<sha1_hash> m_owned_similar_torrents;

		// these or strings of the "collections" key from the torrent file. The
		// first value is the offset into the metadata where the string is, the
		// second value is the length of the string. Strings are not 0-terminated.
		std::vector<std::pair<std::int32_t, int>> m_collections;

		// these are the collections from outside of the info-dict. These are
		// owning strings, since we only keep the info-section around, these
		// cannot be pointers into that buffer.
		std::vector<std::string> m_owned_collections;

#if TORRENT_ABI_VERSION <= 2
		// if this is a merkle torrent, this is the merkle
		// tree. It has space for merkle_num_nodes(merkle_num_leafs(num_pieces))
		// hashes
		aux::vector<sha1_hash> m_merkle_tree;
#endif

		// v2 merkle tree for each file
		// the actual hash buffers are always divisible by 32 (sha256_hash::size())
		aux::vector<aux::vector<char>, file_index_t> m_piece_layers;

		// this is a copy of the info section from the torrent.
		// it use maintained in this flat format in order to
		// make it available through the metadata extension
		// TODO: change the type to std::shared_ptr<char const> in C++17
		// it is used as if immutable, it cannot be const for technical reasons
		// right now.
		boost::shared_array<char> m_info_section;

		// if a comment is found in the torrent file
		// this will be set to that comment
		std::string m_comment;

		// an optional string naming the software used
		// to create the torrent file
		std::string m_created_by;

		// the info section parsed. points into m_info_section
		// parsed lazily
		mutable bdecode_node m_info_dict;

		// if a creation date is found in the torrent file
		// this will be set to that, otherwise it'll be
		// 1970, Jan 1
		std::time_t m_creation_date = 0;

		// the hash(es) that identify this torrent
		info_hash_t m_info_hash;

		// this is the offset into the m_info_section buffer to the first byte of
		// the first SHA-1 hash
		std::int32_t m_piece_hashes = 0;

		// the number of bytes in m_info_section
		std::int32_t m_info_section_size = 0;

		// this is used when creating a torrent. If there's
		// only one file there are cases where it's impossible
		// to know if it should be written as a multi file torrent
		// or not. e.g. test/test  there's one file and one directory
		// and they have the same name.
		static constexpr torrent_info_flags_t multifile = 0_bit;

		// this is true if the torrent is private. i.e., is should not
		// be announced on the dht
		static constexpr torrent_info_flags_t private_torrent = 1_bit;

		// this is true if one of the trackers has an .i2p top
		// domain in its hostname. This means the DHT and LSD
		// features are disabled for this torrent (unless the
		// settings allows mixing i2p peers with regular peers)
		static constexpr torrent_info_flags_t i2p = 2_bit;

		// this flag is set if we found an ssl-cert field in the info
		// dictionary
		static constexpr torrent_info_flags_t ssl_torrent = 3_bit;

		// v2 piece hashes were loaded from the torrent file and verified
		static constexpr torrent_info_flags_t v2_has_piece_hashes = 4_bit;

		torrent_info_flags_t m_flags{};
	};

TORRENT_VERSION_NAMESPACE_3_END

}

#endif // TORRENT_TORRENT_INFO_HPP_INCLUDED
