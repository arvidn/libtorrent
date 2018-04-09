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

#ifndef TORRENT_TORRENT_INFO_HPP_INCLUDED
#define TORRENT_TORRENT_INFO_HPP_INCLUDED

#include <string>
#include <vector>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/optional.hpp>
#include <boost/shared_array.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/copy_ptr.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/file_storage.hpp"

namespace libtorrent
{
	class peer_connection;
	class entry;
	struct announce_entry;
	struct lazy_entry;

	namespace aux { struct session_settings; }

	// internal, exposed for the unit test
	TORRENT_EXTRA_EXPORT void sanitize_append_path_element(std::string& path
		, char const* element, int element_len);
	TORRENT_EXTRA_EXPORT bool verify_encoding(std::string& target);

	// the web_seed_entry holds information about a web seed (also known
	// as URL seed or HTTP seed). It is essentially a URL with some state
	// associated with it. For more information, see `BEP 17`_ and `BEP 19`_.
	struct TORRENT_EXPORT web_seed_entry
	{
		// http seeds are different from url seeds in the
		// protocol they use. http seeds follows the original
		// http seed spec. by John Hoffman
		enum type_t { url_seed, http_seed };

		typedef std::vector<std::pair<std::string, std::string> > headers_t;

		web_seed_entry(std::string const& url_, type_t type_
			, std::string const& auth_ = std::string()
			, headers_t const& extra_headers_ = headers_t());

		// URL and type comparison
		bool operator==(web_seed_entry const& e) const
		{ return url == e.url && type == e.type; }

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
		boost::uint8_t type;
	};

#ifndef BOOST_NO_EXCEPTIONS
	// for backwards compatibility with 0.14
	typedef libtorrent_exception invalid_torrent_file;
#endif

	// TODO: there may be some opportunities to optimize the size if torrent_info.
	// specifically to turn some std::string and std::vector into pointers
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
		// The ``flags`` argument is currently unused.
#ifndef BOOST_NO_EXCEPTIONS
		torrent_info(bdecode_node const& torrent_file, int flags = 0);
		torrent_info(char const* buffer, int size, int flags = 0);
		torrent_info(std::string const& filename, int flags = 0);
#endif // BOOST_NO_EXCEPTIONS
		torrent_info(torrent_info const& t);
		torrent_info(sha1_hash const& info_hash, int flags = 0);
		torrent_info(bdecode_node const& torrent_file, error_code& ec, int flags = 0);
		torrent_info(char const* buffer, int size, error_code& ec, int flags = 0);
		torrent_info(std::string const& filename, error_code& ec, int flags = 0);
#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED
		torrent_info(lazy_entry const& torrent_file, int flags = 0);
		TORRENT_DEPRECATED
		torrent_info(lazy_entry const& torrent_file, error_code& ec
			, int flags = 0);
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11 instead, use the wchar
		// -> utf8 conversion functions and pass in utf8 strings
		TORRENT_DEPRECATED
		torrent_info(std::wstring const& filename, error_code& ec
			, int flags = 0);
		TORRENT_DEPRECATED
		torrent_info(std::wstring const& filename, int flags = 0);
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE

		// frees all storage associated with this torrent_info object
		~torrent_info();

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
		file_storage const& orig_files() const
		{
			TORRENT_ASSERT(is_loaded());
			return m_orig_files ? *m_orig_files : m_files;
		}

		// Renames a the file with the specified index to the new name. The new
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
		void rename_file(int index, std::string const& new_filename)
		{
			TORRENT_ASSERT(is_loaded());
			copy_on_write();
			m_files.rename_file(index, new_filename);
		}
#ifndef TORRENT_NO_DEPRECATE
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11
		// instead, use the wchar -> utf8 conversion functions
		// and pass in utf8 strings
		TORRENT_DEPRECATED
		void rename_file(int index, std::wstring const& new_filename);
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE

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
		//
		// The ``trackers()`` function will return a sorted vector of
		// ``announce_entry``. Each announce entry contains a string, which is
		// the tracker url, and a tier index. The tier index is the high-level
		// priority. No matter which trackers that works or not, the ones with
		// lower tier will always be tried before the one with higher tier
		// number. For more information, see announce_entry_.
		void add_tracker(std::string const& url, int tier = 0);
		std::vector<announce_entry> const& trackers() const { return m_urls; }

		// These two functions are related to `BEP 38`_ (mutable torrents). The
		// vectors returned from these correspond to the "similar" and
		// "collections" keys in the .torrent file. Both info-hashes and
		// collections from within the info-dict and from outside of it are
		// included.
		// 
		// .. _`BEP 38`: http://www.bittorrent.org/beps/bep_0038.html
		std::vector<sha1_hash> similar_torrents() const;
		std::vector<std::string> collections() const;

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16. Use web_seeds() instead
		TORRENT_DEPRECATED
		std::vector<std::string> url_seeds() const;
		TORRENT_DEPRECATED
		std::vector<std::string> http_seeds() const;

		// deprecated in 1.1
		TORRENT_DEPRECATED
		bool parse_info_section(lazy_entry const& e, error_code& ec
			, int flags);
#endif // TORRENT_NO_DEPRECATE

		// ``web_seeds()`` returns all url seeds and http seeds in the torrent.
		// Each entry is a ``web_seed_entry`` and may refer to either a url seed
		// or http seed.
		// 
		// ``add_url_seed()`` and ``add_http_seed()`` adds one url to the list of
		// url/http seeds. Currently, the only transport protocol supported for
		// the url is http.
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
			, std::string const& extern_auth = std::string()
			, web_seed_entry::headers_t const& extra_headers = web_seed_entry::headers_t());
		void add_http_seed(std::string const& url
			, std::string const& extern_auth = std::string()
			, web_seed_entry::headers_t const& extra_headers = web_seed_entry::headers_t());
		std::vector<web_seed_entry> const& web_seeds() const { return m_web_seeds; }
		void set_web_seeds(std::vector<web_seed_entry> seeds);

		// ``total_size()``, ``piece_length()`` and ``num_pieces()`` returns the
		// total number of bytes the torrent-file represents (all the files in
		// it), the number of byte for each piece and the total number of pieces,
		// respectively. The difference between ``piece_size()`` and
		// ``piece_length()`` is that ``piece_size()`` takes the piece index as
		// argument and gives you the exact size of that piece. It will always be
		// the same as ``piece_length()`` except in the case of the last piece,
		// which may be smaller.
		boost::int64_t total_size() const { return m_files.total_size(); }
		int piece_length() const { return m_files.piece_length(); }
		int num_pieces() const { return m_files.num_pieces(); }

		// returns the info-hash of the torrent
		const sha1_hash& info_hash() const { return m_info_hash; }

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.0. Use the variants that take an index instead
		// internal_file_entry is no longer exposed in the API
		typedef file_storage::iterator file_iterator;
		typedef file_storage::reverse_iterator reverse_file_iterator;

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
		file_iterator file_at_offset(boost::int64_t offset) const
		{ return m_files.file_at_offset_deprecated(offset); }

		TORRENT_DEPRECATED
		file_entry file_at(int index) const { return m_files.at_deprecated(index); }
#endif // TORRENT_NO_DEPRECATE

		// If you need index-access to files you can use the ``num_files()`` along
		// with the ``file_path()``, ``file_size()``-family of functions to access
		// files using indices.
		int num_files() const { return m_files.num_files(); }

		// This function will map a piece index, a byte offset within that piece
		// and a size (in bytes) into the corresponding files with offsets where
		// that data for that piece is supposed to be stored. See file_slice.
		std::vector<file_slice> map_block(int piece, boost::int64_t offset, int size) const
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
		peer_request map_file(int file, boost::int64_t offset, int size) const
		{
			TORRENT_ASSERT(is_loaded());
			return m_files.map_file(file, offset, size);
		}

		// load and unload this torrent info
		void load(char const* buffer, int size, error_code& ec);
		void unload();

#ifndef TORRENT_NO_DEPRECATE
// ------- start deprecation -------
// these functions will be removed in a future version
		TORRENT_DEPRECATED
		torrent_info(entry const& torrent_file);
		TORRENT_DEPRECATED
		void print(std::ostream& os) const;
// ------- end deprecation -------
#endif

		// Returns the SSL root certificate for the torrent, if it is an SSL
		// torrent. Otherwise returns an empty string. The certificate is
		// the the public certificate in x509 format.
		std::string ssl_cert() const;

		// returns true if this torrent_info object has a torrent loaded.
		// This is primarily used to determine if a magnet link has had its
		// metadata resolved yet or not.
		bool is_valid() const { return m_files.is_valid(); }

		// returns true if this torrent is private. i.e., it should not be
		// distributed on the trackerless network (the kademlia DHT).
		bool priv() const { return m_private; }

		// returns true if this is an i2p torrent. This is determined by whether
		// or not it has a tracker whose URL domain name ends with ".i2p". i2p
		// torrents disable the DHT and local peer discovery as well as talking
		// to peers over anything other than the i2p network.
		bool is_i2p() const { return m_i2p; }

		// ``hash_for_piece()`` takes a piece-index and returns the 20-bytes
		// sha1-hash for that piece and ``info_hash()`` returns the 20-bytes
		// sha1-hash for the info-section of the torrent file.
		// ``hash_for_piece_ptr()`` returns a pointer to the 20 byte sha1 digest
		// for the piece. Note that the string is not null-terminated.
		int piece_size(int index) const { return m_files.piece_size(index); }
		sha1_hash hash_for_piece(int index) const;
		char const* hash_for_piece_ptr(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < m_files.num_pieces());
			TORRENT_ASSERT(is_loaded());
			if (is_merkle_torrent())
			{
				TORRENT_ASSERT(index < int(m_merkle_tree.size() - m_merkle_first_leaf));
				return m_merkle_tree[m_merkle_first_leaf + index].data();
			}
			else
			{
				TORRENT_ASSERT(m_piece_hashes);
				TORRENT_ASSERT(m_piece_hashes >= m_info_section.get());
				TORRENT_ASSERT(m_piece_hashes < m_info_section.get() + m_info_section_size);
				TORRENT_ASSERT(index < int(m_info_section_size / 20));
				return &m_piece_hashes[index*20];
			}
		}

		bool is_loaded() const { return m_piece_hashes || !m_merkle_tree.empty(); }

		// ``merkle_tree()`` returns a reference to the merkle tree for this
		// torrent, if any.
		// 
		// ``set_merkle_tree()`` moves the passed in merkle tree into the
		// torrent_info object. i.e. ``h`` will not be identical after the call.
		// You need to set the merkle tree for a torrent that you've just created
		// (as a merkle torrent). The merkle tree is retrieved from the
		// ``create_torrent::merkle_tree()`` function, and need to be saved
		// separately from the torrent file itself. Once it's added to
		// libtorrent, the merkle tree will be persisted in the resume data.
		std::vector<sha1_hash> const& merkle_tree() const { return m_merkle_tree; }
		void set_merkle_tree(std::vector<sha1_hash>& h)
		{ TORRENT_ASSERT(h.size() == m_merkle_tree.size() ); m_merkle_tree.swap(h); }

		// ``name()`` returns the name of the torrent.
		// 
		// ``comment()`` returns the comment associated with the torrent. If
		// there's no comment, it will return an empty string.
		// ``creation_date()`` returns the creation date of the torrent as time_t
		// (`posix time`_). If there's no time stamp in the torrent file, the
		// optional object will be uninitialized.
		// 
		// Both the name and the comment is UTF-8 encoded strings.
		// 
		// ``creator()`` returns the creator string in the torrent. If there is
		// no creator string it will return an empty string.
		// 
		// .. _`posix time`: http://www.opengroup.org/onlinepubs/009695399/functions/time.html
		const std::string& name() const { return m_files.name(); }
		boost::optional<time_t> creation_date() const;
		const std::string& creator() const
		{ return m_created_by; }
		const std::string& comment() const
		{ return m_comment; }

		// dht nodes to add to the routing table/bootstrap from
		typedef std::vector<std::pair<std::string, int> > nodes_t;

		// If this torrent contains any DHT nodes, they are put in this vector in
		// their original form (host name and port number).
		nodes_t const& nodes() const
		{ return m_nodes; }

		// This is used when creating torrent. Use this to add a known DHT node.
		// It may be used, by the client, to bootstrap into the DHT network.
		void add_node(std::pair<std::string, int> const& node)
		{ m_nodes.push_back(node); }

		// populates the torrent_info by providing just the info-dict buffer.
		// This is used when loading a torrent from a magnet link for instance,
		// where we only have the info-dict. The bdecode_node ``e`` points to a
		// parsed info-dictionary. ``ec`` returns an error code if something
		// fails (typically if the info dictionary is malformed). ``flags`` are
		// currently unused.
		bool parse_info_section(bdecode_node const& e, error_code& ec, int flags);

		// This function looks up keys from the info-dictionary of the loaded
		// torrent file. It can be used to access extension values put in the
		// .torrent file. If the specified key cannot be found, it returns NULL.
		bdecode_node info(char const* key) const;

		// swap the content of this and ``ti``.
		void swap(torrent_info& ti);

		// ``metadata()`` returns a the raw info section of the torrent file. The size
		// of the metadata is returned by ``metadata_size()``.
		int metadata_size() const { return m_info_section_size; }
		boost::shared_array<char> metadata() const
		{ return m_info_section; }

		// internal
		bool add_merkle_nodes(std::map<int, sha1_hash> const& subtree
			, int piece);
		std::map<int, sha1_hash> build_merkle_list(int piece) const;

		// returns whether or not this is a merkle torrent.
		// see `BEP 30`__.
		//
		// __ http://bittorrent.org/beps/bep_0030.html
		bool is_merkle_torrent() const { return !m_merkle_tree.empty(); }

		bool parse_torrent_file(bdecode_node const& libtorrent, error_code& ec, int flags);

		// if we're logging member offsets, we need access to them
	private:

		void resolve_duplicate_filenames();

		// the slow path, in case we detect/suspect a name collision
		void resolve_duplicate_filenames_slow();

#if TORRENT_USE_INVARIANT_CHECKS
		friend class invariant_access;
		void check_invariant() const;
#endif

		// not assignable
		torrent_info const& operator=(torrent_info const&);

		void copy_on_write();

		file_storage m_files;

		// if m_files is modified, it is first copied into
		// m_orig_files so that the original name and
		// filenames are preserved.
		copy_ptr<const file_storage> m_orig_files;

		// the URLs to the trackers
		std::vector<announce_entry> m_urls;
		std::vector<web_seed_entry> m_web_seeds;
		nodes_t m_nodes;

		// the info-hashes (20 bytes each) in the "similar" key. The pointers
		// point directly into the info_section. When copied, these pointers must
		// be corrected to point into the copied-to buffer
		std::vector<char const*> m_similar_torrents;

		// these are similar torrents from outside of the info-dict. We can't
		// have non-owning pointers to those, as we only keep the info-dict
		// around.
		std::vector<sha1_hash> m_owned_similar_torrents;

		// these or strings of the "collections" key from the torrent file. The
		// pointers point directly into the info_section buffer and when copied,
		// these pointers must be corrected to point into the new buffer. The
		// int is the length of the string. Strings are not NULL-terminated.
		std::vector<std::pair<char const*, int> > m_collections;

		// these are the collections from outside of the info-dict. These are
		// owning strings, since we only keep the info-section around, these
		// cannot be pointers into that buffer.
		std::vector<std::string> m_owned_collections;

		// if this is a merkle torrent, this is the merkle
		// tree. It has space for merkle_num_nodes(merkle_num_leafs(num_pieces))
		// hashes
		std::vector<sha1_hash> m_merkle_tree;

		// this is a copy of the info section from the torrent.
		// it use maintained in this flat format in order to
		// make it available through the metadata extension
		boost::shared_array<char> m_info_section;

		// this is a pointer into the m_info_section buffer
		// pointing to the first byte of the first SHA-1 hash
		char const* m_piece_hashes;

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
		time_t m_creation_date;

		// the hash that identifies this torrent
		sha1_hash m_info_hash;

		// the number of bytes in m_info_section
		boost::uint32_t m_info_section_size;

		// the index to the first leaf. This is where the hash for the
		// first piece is stored
		boost::uint32_t m_merkle_first_leaf:24;

		// this is used when creating a torrent. If there's
		// only one file there are cases where it's impossible
		// to know if it should be written as a multi file torrent
		// or not. e.g. test/test  there's one file and one directory
		// and they have the same name.
		bool m_multifile:1;

		// this is true if the torrent is private. i.e., is should not
		// be announced on the dht
		bool m_private:1;

		// this is true if one of the trackers has an .i2p top
		// domain in its hostname. This means the DHT and LSD
		// features are disabled for this torrent (unless the
		// settings allows mixing i2p peers with regular peers)
		bool m_i2p:1;
	};

}

#endif // TORRENT_TORRENT_INFO_HPP_INCLUDED

