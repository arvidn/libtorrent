/*

Copyright (c) 2008-2020, Arvid Norberg
Copyright (c) 2016, Markus
Copyright (c) 2016-2017, 2019, Alden Torres
Copyright (c) 2017, Steven Siloti
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

#ifndef TORRENT_CREATE_TORRENT_HPP_INCLUDED
#define TORRENT_CREATE_TORRENT_HPP_INCLUDED

#include "libtorrent/bencode.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path etc.
#include "libtorrent/fwd.hpp"
#include "libtorrent/aux_/throw.hpp"

#include <vector>
#include <string>
#include <utility>

// OVERVIEW
//
// This section describes the functions and classes that are used
// to create torrent files. It is a layered API with low level classes
// and higher level convenience functions. A torrent is created in 4
// steps:
//
// 1. first the files that will be part of the torrent are determined.
// 2. the torrent properties are set, such as tracker url, web seeds,
//    DHT nodes etc.
// 3. Read through all the files in the torrent, SHA-1 all the data
//    and set the piece hashes.
// 4. The torrent is bencoded into a file or buffer.
//
// If there are a lot of files and or deep directory hierarchies to
// traverse, step one can be time consuming.
//
// Typically step 3 is by far the most time consuming step, since it
// requires to read all the bytes from all the files in the torrent.
//
// All of these classes and functions are declared by including
// ``libtorrent/create_torrent.hpp``.
//
// example:
//
// .. code:: c++
//
//	file_storage fs;
//
//	// recursively adds files in directories
//	add_files(fs, "./my_torrent");
//
//	create_torrent t(fs);
//	t.add_tracker("http://my.tracker.com/announce");
//	t.set_creator("libtorrent example");
//
//	// reads the files and calculates the hashes
//	set_piece_hashes(t, ".");
//
//	ofstream out("my_torrent.torrent", std::ios_base::binary);
//	bencode(std::ostream_iterator<char>(out), t.generate());
//
namespace libtorrent {

	// hidden
	using create_flags_t = flags::bitfield_flag<std::uint32_t, struct create_flags_tag>;

	// This class holds state for creating a torrent. After having added
	// all information to it, call create_torrent::generate() to generate
	// the torrent. The entry that's returned can then be bencoded into a
	// .torrent file using bencode().
	struct TORRENT_EXPORT create_torrent
	{
#if TORRENT_ABI_VERSION == 1
		using flags_t = create_flags_t;
#endif

#if TORRENT_ABI_VERSION <= 2
		// This will insert pad files to align the files to piece boundaries, for
		// optimized disk-I/O. This will minimize the number of bytes of pad-
		// files, to keep the impact down for clients that don't support
		// them.
		// incompatible with v2 metadata, ignored
		TORRENT_DEPRECATED static constexpr create_flags_t optimize_alignment = 0_bit;
#endif
#if TORRENT_ABI_VERSION == 1
		// same as optimize_alignment, for backwards compatibility
		TORRENT_DEPRECATED static constexpr create_flags_t optimize = 0_bit;
#endif

#if TORRENT_ABI_VERSION <= 2
		// This will create a merkle hash tree torrent. A merkle torrent cannot
		// be opened in clients that don't specifically support merkle torrents.
		// The benefit is that the resulting torrent file will be much smaller and
		// not grow with more pieces. When this option is specified, it is
		// recommended to have a fairly small piece size, say 64 kiB.
		// When creating merkle torrents, the full hash tree is also generated
		// and should be saved off separately. It is accessed through the
		// create_torrent::merkle_tree() function.
		// support for BEP 30 merkle torrents has been removed
		TORRENT_DEPRECATED static constexpr create_flags_t merkle = 1_bit;
#endif

		// This will include the file modification time as part of the torrent.
		// This is not enabled by default, as it might cause problems when you
		// create a torrent from separate files with the same content, hoping to
		// yield the same info-hash. If the files have different modification times,
		// with this option enabled, you would get different info-hashes for the
		// files.
		static constexpr create_flags_t modification_time = 2_bit;

		// If this flag is set, files that are symlinks get a symlink attribute
		// set on them and their data will not be included in the torrent. This
		// is useful if you need to reconstruct a file hierarchy which contains
		// symlinks.
		static constexpr create_flags_t symlinks = 3_bit;

		// to create a torrent that can be updated via a *mutable torrent*
		// (see `BEP 38`_). This also needs to be enabled for torrents that update
		// another torrent.
#if TORRENT_ABI_VERSION <= 2
		// BEP 52 requires files to be piece aligned so all torrents are now compatible
		// with BEP 38
		TORRENT_DEPRECATED static constexpr create_flags_t mutable_torrent_support = 4_bit;
#endif

		// Do not generate v1 metadata. The resulting torrent will only be usable by
		// clients which support v2. This requires setting all v2 hashes, with
		// set_hash2() before calling generate(). Setting v1 hashes (with
		// set_hash()) is an error with this flag set.
		static constexpr create_flags_t v2_only = 5_bit;

		// do not generate v2 metadata or enforce v2 alignment and padding rules
		// this is mainly for tests, not recommended for production use. This
		// requires setting all v1 hashes, with set_hash(), before calling
		// generate(). Setting v2 hashes (with set_hash2()) is an error with
		// this flag set.
		static constexpr create_flags_t v1_only = 6_bit;

		// This flag only affects v1-only torrents, and is only relevant
		// together with the v1_only_flag. This flag will force the
		// same file order and padding as a v2 (or hybrid) torrent would have.
		// It has the effect of ordering files and inserting pad files to align
		// them with piece boundaries.
		static constexpr create_flags_t canonical_files = 7_bit;

		// The ``piece_size`` is the size of each piece in bytes. It must be a
		// power of 2 and a minimum of 16 kiB. If a piece size of 0 is
		// specified, a piece_size will be set automatically.
		//
		// The overload that takes a ``torrent_info`` object will make a verbatim
		// copy of its info dictionary (to preserve the info-hash). The copy of
		// the info dictionary will be used by create_torrent::generate(). This means
		// that none of the member functions of create_torrent that affects
		// the content of the info dictionary (such as set_hash()), will
		// have any affect.
		//
		// The ``flags`` arguments specifies options for the torrent creation. It can
		// be any combination of the flags defined by create_flags_t.
		explicit create_torrent(file_storage& fs, int piece_size = 0
			, create_flags_t flags = {});
		explicit create_torrent(torrent_info const& ti);

#if TORRENT_ABI_VERSION <= 2
		TORRENT_DEPRECATED
		explicit create_torrent(file_storage& fs, int piece_size
			, int, create_flags_t flags = {}, int = -1)
			: create_torrent(fs, piece_size, flags) {}
#endif

		// internal
		~create_torrent();

		// This function will generate the .torrent file as a bencode tree. In order to
		// generate the flat file, use the bencode() function.
		//
		// It may be useful to add custom entries to the torrent file before bencoding it
		// and saving it to disk.
		//
		// Whether the resulting torrent object is v1, v2 or hybrid depends on
		// whether any of the v1_only or v2_only flags were set on the
		// constructor. If neither were set, the resulting torrent depends on
		// which hashes were set. If both v1 and v2 hashes were set, a hybrid
		// torrent is created.
		//
		// Any failure will cause this function to throw system_error, with an
		// appropriate error message. These are the reasons this call may throw:
		//
		// * the file storage has 0 files
		// * the total size of the file storage is 0 bytes (i.e. it only has
		//   empty files)
		// * not all v1 hashes (set_hash()) and not all v2 hashes (set_hash2())
		//   were set
		// * for v2 torrents, you may not have a directory with the same name as
		//   a file. If that's encountered in the file storage, generate()
		//   fails.
		entry generate() const;

		// returns an immutable reference to the file_storage used to create
		// the torrent from.
		file_storage const& files() const { return m_files; }

		// Sets the comment for the torrent. The string ``str`` should be utf-8 encoded.
		// The comment in a torrent file is optional.
		void set_comment(char const* str);

		// Sets the creator of the torrent. The string ``str`` should be utf-8 encoded.
		// This is optional.
		void set_creator(char const* str);

		// sets the "creation time" field. Defaults to the system clock at the
		// time of construction of the create_torrent object. The timestamp is
		// specified in seconds, posix time. If the creation date is set to 0,
		// the "creation date" field will be omitted from the generated torrent.
		void set_creation_date(std::time_t timestamp);

		// This sets the SHA-1 hash for the specified piece (``index``). You are required
		// to set the hash for every piece in the torrent before generating it. If you have
		// the files on disk, you can use the high level convenience function to do this.
		// See set_piece_hashes().
		// A SHA-1 hash of all zeros is internally used to indicate a hash that
		// has not been set. Setting such hash will not be considered set when
		// calling generate().
		// This function will throw ``std::system_error`` if it is called on an
		// object constructed with the v2_only flag.
		void set_hash(piece_index_t index, sha1_hash const& h);

		// sets the bittorrent v2 hash for file `file` of the piece `piece`.
		// `piece` is relative to the first piece of the file, starting at 0. The
		// first piece in the file can be computed with
		// file_storage::file_index_at_piece().
		// The hash, `h`, is the root of the merkle tree formed by the piece's
		// 16 kiB blocks. Note that piece sizes must be powers-of-2, so all
		// per-piece merkle trees are complete.
		// A SHA-256 hash of all zeros is internally used to indicate a hash
		// that has not been set. Setting such hash will not be considered set
		// when calling generate().
		// This function will throw ``std::system_error`` if it is called on an
		// object constructed with the v1_only flag.
		void set_hash2(file_index_t file, piece_index_t::diff_type piece, sha256_hash const& h);

#if TORRENT_ABI_VERSION < 3
		// This sets the sha1 hash for this file. This hash will end up under the key ``sha1``
		// associated with this file (for multi-file torrents) or in the root info dictionary
		// for single-file torrents.
		// .. note::
		//
		// 	with bittorrent v2, this feature is obsolete
		TORRENT_DEPRECATED
		void set_file_hash(file_index_t index, sha1_hash const& h);
#endif

		// This adds a url seed to the torrent. You can have any number of url seeds. For a
		// single file torrent, this should be an HTTP url, pointing to a file with identical
		// content as the file of the torrent. For a multi-file torrent, it should point to
		// a directory containing a directory with the same name as this torrent, and all the
		// files of the torrent in it.
		//
		// The second function, ``add_http_seed()`` adds an HTTP seed instead.
		void add_url_seed(string_view url);
		void add_http_seed(string_view url);

		// This adds a DHT node to the torrent. This especially useful if you're creating a
		// tracker less torrent. It can be used by clients to bootstrap their DHT node from.
		// The node is a hostname and a port number where there is a DHT node running.
		// You can have any number of DHT nodes in a torrent.
		void add_node(std::pair<std::string, int> node);

		// Adds a tracker to the torrent. This is not strictly required, but most torrents
		// use a tracker as their main source of peers. The url should be an http:// or udp://
		// url to a machine running a bittorrent tracker that accepts announces for this torrent's
		// info-hash. The tier is the fallback priority of the tracker. All trackers with tier 0 are
		// tried first (in any order). If all fail, trackers with tier 1 are tried. If all of those
		// fail, trackers with tier 2 are tried, and so on.
		void add_tracker(string_view url, int tier = 0);

		// This function sets an X.509 certificate in PEM format to the torrent. This makes the
		// torrent an *SSL torrent*. An SSL torrent requires that each peer has a valid certificate
		// signed by this root certificate. For SSL torrents, all peers are connecting over SSL
		// connections. For more information, see the section on ssl-torrents_.
		//
		// The string is not the path to the cert, it's the actual content of the
		// certificate.
		void set_root_cert(string_view cert);

		// Sets and queries the private flag of the torrent.
		// Torrents with the private flag set ask the client to not use any other
		// sources than the tracker for peers, and to not use DHT to advertise itself publicly,
		// only the tracker.
		void set_priv(bool p) { m_private = p; }
		bool priv() const { return m_private; }

		bool is_v2_only() const { return m_v2_only; }
		bool is_v1_only() const { return m_v1_only; }

		// returns the number of pieces in the associated file_storage object.
		int num_pieces() const { return m_files.num_pieces(); }

		// ``piece_length()`` returns the piece size of all pieces but the
		// last one. ``piece_size()`` returns the size of the specified piece.
		// these functions are just forwarding to the associated file_storage.
		int piece_length() const { return m_files.piece_length(); }
		int piece_size(piece_index_t i) const { return m_files.piece_size(i); }

#if TORRENT_ABI_VERSION <= 2
		// support for BEP 30 merkle torrents has been removed

		// This function returns the merkle hash tree, if the torrent was created as a merkle
		// torrent. The tree is created by ``generate()`` and won't be valid until that function
		// has been called. When creating a merkle tree torrent, the actual tree itself has to
		// be saved off separately and fed into libtorrent the first time you start seeding it,
		// through the ``torrent_info::set_merkle_tree()`` function. From that point onwards, the
		// tree will be saved in the resume data.
		TORRENT_DEPRECATED
		std::vector<sha1_hash> merkle_tree() const { return std::vector<sha1_hash>(); }
#endif

		// Add similar torrents (by info-hash) or collections of similar torrents.
		// Similar torrents are expected to share some files with this torrent.
		// Torrents sharing a collection name with this torrent are also expected
		// to share files with this torrent. A torrent may have more than one
		// collection and more than one similar torrents. For more information,
		// see `BEP 38`_.
		void add_similar_torrent(sha1_hash ih);
		void add_collection(string_view c);

	private:

		file_storage& m_files;
		// if m_info_dict is initialized, it is
		// used instead of m_files to generate
		// the info dictionary
		entry m_info_dict;

		// the URLs to the trackers
		std::vector<std::pair<std::string, int>> m_urls;

		std::vector<std::string> m_url_seeds;
		std::vector<std::string> m_http_seeds;

		aux::vector<sha1_hash, piece_index_t> m_piece_hash;

		// leave this here for now, to preserve ABI between building with
		// deprecated functions and without
		aux::vector<sha1_hash, file_index_t> m_filehashes;

		mutable aux::vector<sha256_hash, file_index_t> m_fileroots;
		aux::vector<aux::vector<sha256_hash, piece_index_t::diff_type>, file_index_t> m_file_piece_hash;

		std::vector<sha1_hash> m_similar;
		std::vector<std::string> m_collections;

		// dht nodes to add to the routing table/bootstrap from
		std::vector<std::pair<std::string, int>> m_nodes;

		// if a creation date is found in the torrent file
		// this will be set to that, otherwise it'll be
		// 1970, Jan 1
		time_t m_creation_date;

		// if a comment is found in the torrent file
		// this will be set to that comment
		std::string m_comment;

		// an optional string naming the software used
		// to create the torrent file
		std::string m_created_by;

		// this is the root cert for SSL torrents
		std::string m_root_cert;

		// this is used when creating a torrent. If there's
		// only one file there are cases where it's impossible
		// to know if it should be written as a multi-file torrent
		// or not. e.g. test/test  there's one file and one directory
		// and they have the same name.
		bool m_multifile:1;

		// this is true if the torrent is private. i.e., the client should not
		// advertise itself on the DHT for this torrent
		bool m_private:1;

		// if set, include the 'mtime' modification time in the
		// torrent file
		bool m_include_mtime:1;

		// if set, symbolic links are declared as such in
		// the torrent file. The full data of the pointed-to
		// file is still included
		bool m_include_symlinks:1;

		bool m_v2_only:1;

		// only generate v1 metadata and do not enforce v2 padding rules
		bool m_v1_only:1;
	};

namespace aux {
	inline void nop(piece_index_t) {}
}

	// Adds the file specified by ``path`` to the file_storage object. In case ``path``
	// refers to a directory, files will be added recursively from the directory.
	//
	// If specified, the predicate ``p`` is called once for every file and directory that
	// is encountered. Files for which ``p`` returns true are added, and directories for
	// which ``p`` returns true are traversed. ``p`` must have the following signature:
	//
	// .. code:: c++
	//
	// 	bool Pred(std::string const& p);
	//
	// The path that is passed in to the predicate is the full path of the file or
	// directory. If no predicate is specified, all files are added, and all directories
	// are traversed.
	//
	// The ".." directory is never traversed.
	//
	// The ``flags`` argument should be the same as the flags passed to the `create_torrent`_
	// constructor.
	TORRENT_EXPORT void add_files(file_storage& fs, std::string const& file
		, std::function<bool(std::string)> p, create_flags_t flags = {});
	TORRENT_EXPORT void add_files(file_storage& fs, std::string const& file
		, create_flags_t flags = {});

	// This function will assume that the files added to the torrent file exists at path
	// ``p``, read those files and hash the content and set the hashes in the ``create_torrent``
	// object. The optional function ``f`` is called in between every hash that is set. ``f``
	// must have the following signature:
	//
	// .. code:: c++
	//
	// 	void Fun(piece_index_t);
	//
	// The overloads taking a settings_pack may be used to configure the
	// underlying disk access. Such as ``settings_pack::aio_threads``.
	//
	// The overloads that don't take an ``error_code&`` may throw an exception in case of a
	// file error, the other overloads sets the error code to reflect the error, if any.
	TORRENT_EXPORT void set_piece_hashes(create_torrent& t, std::string const& p
		, std::function<void(piece_index_t)> const& f, error_code& ec);
	TORRENT_EXPORT void set_piece_hashes(create_torrent& t, std::string const& p
		, settings_interface const& settings
		, std::function<void(piece_index_t)> const& f, error_code& ec);
	inline void set_piece_hashes(create_torrent& t, std::string const& p, error_code& ec)
	{
		set_piece_hashes(t, p, aux::nop, ec);
	}
#ifndef BOOST_NO_EXCEPTIONS
	inline void set_piece_hashes(create_torrent& t, std::string const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, aux::nop, ec);
		if (ec) aux::throw_ex<system_error>(ec);
	}
	inline void set_piece_hashes(create_torrent& t, std::string const& p
		, std::function<void(piece_index_t)> const& f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) aux::throw_ex<system_error>(ec);
	}
	inline void set_piece_hashes(create_torrent& t, std::string const& p
		, settings_interface const& settings
		, std::function<void(piece_index_t)> const& f)
	{
		error_code ec;
		set_piece_hashes(t, p, settings, f, ec);
		if (ec) aux::throw_ex<system_error>(ec);
	}
#endif

namespace aux {
	TORRENT_EXTRA_EXPORT file_flags_t get_file_attributes(std::string const& p);
	TORRENT_EXTRA_EXPORT std::string get_symlink_path(std::string const& p);
}

}

#endif
