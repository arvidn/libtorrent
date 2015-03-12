/*

Copyright (c) 2003-2014, Arvid Norberg
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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/optional.hpp>
#include <boost/shared_array.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/copy_ptr.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/torrent_peer.hpp"

namespace libtorrent
{
	class peer_connection;

	namespace aux { struct session_settings; }
	// exposed for the unit test
	TORRENT_EXTRA_EXPORT void sanitize_append_path_element(std::string& path
		, char const* element, int element_len);

	enum
	{
		// wait at least 5 seconds before retrying a failed tracker
		tracker_retry_delay_min = 5
		// when tracker_failed_max trackers
		// has failed, wait 60 minutes instead
		, tracker_retry_delay_max = 60 * 60
	};

	TORRENT_EXTRA_EXPORT int merkle_num_leafs(int);
	TORRENT_EXTRA_EXPORT int merkle_num_nodes(int);
	TORRENT_EXTRA_EXPORT int merkle_get_parent(int);
	TORRENT_EXTRA_EXPORT int merkle_get_sibling(int);

	// this class holds information about one bittorrent tracker, as it
	// relates to a specific torrent.
	struct TORRENT_EXPORT announce_entry
	{
		// constructs a tracker announce entry with ``u`` as the URL.
		announce_entry(std::string const& u);
		announce_entry();
		~announce_entry();

		// tracker URL as it appeared in the torrent file
		std::string url;

		// the current ``&trackerid=`` argument passed to the tracker.
		// this is optional and is normally empty (in which case no
		// trackerid is sent).
		std::string trackerid;

		// if this tracker has returned an error or warning message
		// that message is stored here
		std::string message;

		// if this tracker failed the last time it was contacted
		// this error code specifies what error occurred
		error_code last_error;

		// returns the number of seconds to the next announce on this tracker.
		// ``min_announce_in()`` returns the number of seconds until we are
		// allowed to force another tracker update with this tracker.
		// 
		// If the last time this tracker was contacted failed, ``last_error`` is
		// the error code describing what error occurred.
		int next_announce_in() const;
		int min_announce_in() const;

		// the time of next tracker announce
		time_point next_announce;

		// no announces before this time
		time_point min_announce;

		// TODO: include the number of peers received from this tracker, at last
		// announce

		// these are either -1 or the scrape information this tracker last
		// responded with. *incomplete* is the current number of downloaders in
		// the swarm, *complete* is the current number of seeds in the swarm and
		// *downloaded* is the cumulative number of completed downloads of this
		// torrent, since the beginning of time (from this tracker's point of
		// view).

		// if this tracker has returned scrape data, these fields are filled in
		// with valid numbers. Otherwise they are set to -1. the number of
		// current downloaders
		int scrape_incomplete;
		int scrape_complete;
		int scrape_downloaded;

		// the tier this tracker belongs to
		boost::uint8_t tier;

		// the max number of failures to announce to this tracker in
		// a row, before this tracker is not used anymore. 0 means unlimited
		boost::uint8_t fail_limit;

		// the number of times in a row we have failed to announce to this
		// tracker.
		boost::uint8_t fails:7;

		// true while we're waiting for a response from the tracker.
		bool updating:1;

		// flags for the source bitmask, each indicating where
		// we heard about this tracker
		enum tracker_source
		{
			// the tracker was part of the .torrent file
			source_torrent = 1,
			// the tracker was added programatically via the add_troacker()_ function
			source_client = 2,
			// the tracker was part of a magnet link
			source_magnet_link = 4,
			// the tracker was received from the swarm via tracker exchange
			source_tex = 8
		};

		// a bitmask specifying which sources we got this tracker from.
		boost::uint8_t source:4;

		// set to true the first time we receive a valid response
		// from this tracker.
		bool verified:1;

		// set to true when we get a valid response from an announce
		// with event=started. If it is set, we won't send start in the subsequent
		// announces.
		bool start_sent:1;

		// set to true when we send a event=completed.
		bool complete_sent:1;

		// this is false the stats sent to this tracker will be 0
		bool send_stats:1;

		// reset announce counters and clears the started sent flag.
		// The announce_entry will look like we've never talked to
		// the tracker.
		void reset();

		// updates the failure counter and time-outs for re-trying.
		// This is called when the tracker announce fails.
		void failed(aux::session_settings const& sett, int retry_interval = 0);

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.0
		TORRENT_DEPRECATED_PREFIX
		bool will_announce(time_point now) const TORRENT_DEPRECATED
		{
			return now <= next_announce
				&& (fails < fail_limit || fail_limit == 0)
				&& !updating;
		}
#endif

		// returns true if we can announec to this tracker now.
		// The current time is passed in as ``now``. The ``is_seed``
		// argument is necessary because once we become a seed, we
		// need to announce right away, even if the re-announce timer
		// hasn't expired yet.
		bool can_announce(time_point now, bool is_seed) const;

		// returns true if the last time we tried to announce to this
		// tracker succeeded, or if we haven't tried yet.
		bool is_working() const
		{ return fails == 0; }

		// trims whitespace characters from the beginning of the URL.
		void trim();
	};

	// the web_seed_entry holds information about a web seed (also known
	// as URL seed or HTTP seed). It is essentially a URL with some state
	// associated with it. For more information, see `BEP 17`_ and `BEP 19`_.
	struct web_seed_entry
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
#ifndef TORRENT_NO_DEPRECATE
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11 instead, use the wchar
		// -> utf8 conversion functions and pass in utf8 strings
		TORRENT_DEPRECATED_PREFIX
		torrent_info(std::wstring const& filename, int flags = 0) TORRENT_DEPRECATED;
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE
#endif // BOOST_NO_EXCEPTIONS
		torrent_info(torrent_info const& t);
		torrent_info(sha1_hash const& info_hash, int flags = 0);
		torrent_info(bdecode_node const& torrent_file, error_code& ec, int flags = 0);
		torrent_info(char const* buffer, int size, error_code& ec, int flags = 0);
		torrent_info(std::string const& filename, error_code& ec, int flags = 0);
#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		torrent_info(lazy_entry const& torrent_file, int flags = 0) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		torrent_info(lazy_entry const& torrent_file, error_code& ec
			, int flags = 0) TORRENT_DEPRECATED;
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11 instead, use the wchar
		// -> utf8 conversion functions and pass in utf8 strings
		TORRENT_DEPRECATED_PREFIX
		torrent_info(std::wstring const& filename, error_code& ec
			, int flags = 0) TORRENT_DEPRECATED;
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
		// to request files with the original names. Filename may be chaged using
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
		// If you want to rename the base name of the torrent (for a multifile
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
		TORRENT_DEPRECATED_PREFIX
		void rename_file(int index, std::wstring const& new_filename) TORRENT_DEPRECATED;
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

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16. Use web_seeds() instead
		TORRENT_DEPRECATED_PREFIX
		std::vector<std::string> url_seeds() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		std::vector<std::string> http_seeds() const TORRENT_DEPRECATED;

		// deprecated in 1.1
		TORRENT_DEPRECATED_PREFIX
		bool parse_info_section(lazy_entry const& e, error_code& ec
			, int flags) TORRENT_DEPRECATED;
#endif // TORRENT_NO_DEPRECATE

		// ``web_seeds()`` returns all url seeds and http seeds in the torrent.
		// Each entry is a ``web_seed_entry`` and may refer to either a url seed
		// or http seed.
		// 		
		// ``add_url_seed()`` and ``add_http_seed()`` adds one url to the list of
		// url/http seeds. Currently, the only transport protocol supported for
		// the url is http.
		// 
		// The ``extern_auth`` argument can be used for other athorization
		// schemese than basic HTTP authorization. If set, it will override any
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
		std::vector<web_seed_entry> const& web_seeds() const
		{ return m_web_seeds; }

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
		TORRENT_DEPRECATED_PREFIX
		file_iterator begin_files() const TORRENT_DEPRECATED { return m_files.begin_deprecated(); }
		TORRENT_DEPRECATED_PREFIX
		file_iterator end_files() const TORRENT_DEPRECATED { return m_files.end_deprecated(); }
		reverse_file_iterator rbegin_files() const TORRENT_DEPRECATED { return m_files.rbegin_deprecated(); }
		TORRENT_DEPRECATED_PREFIX
		reverse_file_iterator rend_files() const TORRENT_DEPRECATED { return m_files.rend_deprecated(); }

		TORRENT_DEPRECATED_PREFIX
		file_iterator file_at_offset(boost::int64_t offset) const TORRENT_DEPRECATED
		{ return m_files.file_at_offset_deprecated(offset); }

		TORRENT_DEPRECATED_PREFIX
		file_entry file_at(int index) const TORRENT_DEPRECATED{ return m_files.at(index); }
#endif // TORRENT_NO_DEPRECATE

		// If you need index-access to files you can use the ``num_files()`` and
		// ``file_at()`` to access files using indices.
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
		TORRENT_DEPRECATED_PREFIX
		torrent_info(entry const& torrent_file) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void print(std::ostream& os) const TORRENT_DEPRECATED;
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
		sha1_hash hash_for_piece(int index) const
		{ return sha1_hash(hash_for_piece_ptr(index)); }
		char const* hash_for_piece_ptr(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < m_files.num_pieces());
			TORRENT_ASSERT(is_loaded());
			if (is_merkle_torrent())
			{
				TORRENT_ASSERT(index < int(m_merkle_tree.size() - m_merkle_first_leaf));
				return (const char*)&m_merkle_tree[m_merkle_first_leaf + index][0];
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
		bdecode_node info(char const* key) const
		{
			if (m_info_dict.type() == bdecode_node::none_t)
			{
				error_code ec;
				bdecode(m_info_section.get(), m_info_section.get()
					+ m_info_section_size, m_info_dict, ec);
				if (ec) return bdecode_node();
			}
			return m_info_dict.dict_find(key);
		}

		// swap the content of this and ``ti```.
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
		// see BEP30__.
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

		// the urls to the trackers
		std::vector<announce_entry> m_urls;
		std::vector<web_seed_entry> m_web_seeds;
		nodes_t m_nodes;

		// if this is a merkle torrent, this is the merkle
		// tree. It has space for merkle_num_nodes(merkle_num_leafs(num_pieces))
		// hashes
		std::vector<sha1_hash> m_merkle_tree;

		// this is a copy of the info section from the torrent.
		// it use maintained in this flat format in order to
		// make it available through the metadata extension
		boost::shared_array<char> m_info_section;

		// this is a pointer into the m_info_section buffer
		// pointing to the first byte of the first sha-1 hash
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

		// the index to the first leaf. This is where the hash for the
		// first piece is stored
		boost::uint32_t m_merkle_first_leaf;

		// the number of bytes in m_info_section
		boost::uint32_t m_info_section_size:24;

		// this is used when creating a torrent. If there's
		// only one file there are cases where it's impossible
		// to know if it should be written as a multifile torrent
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

