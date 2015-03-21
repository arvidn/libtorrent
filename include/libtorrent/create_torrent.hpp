/*

Copyright (c) 2008-2014, Arvid Norberg
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
#include "libtorrent/peer_id.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/file.hpp" // for combine_path etc.

#include <vector>
#include <string>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/scoped_ptr.hpp>
#include <boost/config.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
// If there are a lot of files and or deep directoy hierarchies to
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
namespace libtorrent
{
	class torrent_info;

	// This class holds state for creating a torrent. After having added
	// all information to it, call create_torrent::generate() to generate
	// the torrent. The entry that's returned can then be bencoded into a
	// .torrent file using bencode().
	struct TORRENT_EXPORT create_torrent
	{
		// flags for create_torrent::create_torrent().
		enum flags_t
		{
			// This will insert pad files to align the files to piece boundaries, for
			// optimized disk-I/O.
			optimize = 1

			// This will create a merkle hash tree torrent. A merkle torrent cannot
			// be opened in clients that don't specifically support merkle torrents.
			// The benefit is that the resulting torrent file will be much smaller and
			// not grow with more pieces. When this option is specified, it is
			// recommended to have a fairly small piece size, say 64 kiB.
			// When creating merkle torrents, the full hash tree is also generated
			// and should be saved off separately. It is accessed through the 
			// create_torrent::merkle_tree() function.
			, merkle = 2

			// This will include the file modification time as part of the torrent.
			// This is not enabled by default, as it might cause problems when you
			// create a torrent from separate files with the same content, hoping to
			// yield the same info-hash. If the files have different modification times,
			// with this option enabled, you would get different info-hashes for the
			// files.
			, modification_time = 4

			// If this flag is set, files that are symlinks get a symlink attribute
			// set on them and their data will not be included in the torrent. This
			// is useful if you need to reconstruct a file hierarchy which contains
			// symlinks.
			, symlinks = 8

			// If this is set, the set_piece_hashes() function will, as it calculates
			// the piece hashes, also calculate the file hashes and add those associated
			// with each file. Note that unless you use the set_piece_hashes() function,
			// this flag will have no effect.
			, calculate_file_hashes = 16
		};

		// The ``piece_size`` is the size of each piece in bytes. It must
		// be a multiple of 16 kiB. If a piece size of 0 is specified, a
		// piece_size will be calculated such that the torrent file is roughly 40 kB.
		// 
		// If a ``pad_size_limit`` is specified (other than -1), any file larger than
		// the specified number of bytes will be preceeded by a pad file to align it
		// with the start of a piece. The pad_file_limit is ignored unless the
		// ``optimize`` flag is passed. Typically it doesn't make sense to set this
		// any lower than 4kiB.
		// 
		// The overload that takes a ``torrent_info`` object will make a verbatim
		// copy of its info dictionary (to preserve the info-hash). The copy of
		// the info dictionary will be used by create_torrent::generate(). This means
		// that none of the member functions of create_torrent that affects
		// the content of the info dictionary (such as ``set_hash()``), will
		// have any affect.
		// 
		// The ``flags`` arguments specifies options for the torrent creation. It can
		// be any combination of the flags defined by create_torrent::flags_t.
		// 
		// ``alignment`` is used when pad files are enabled. This is the size
		// eligible files are aligned to. The default is -1, which means the
		// piece size of the torrent.
		create_torrent(file_storage& fs, int piece_size = 0
			, int pad_file_limit = -1, int flags = optimize, int alignment = -1);
		create_torrent(torrent_info const& ti);

		// internal
		~create_torrent();

		// This function will generate the .torrent file as a bencode tree. In order to
		// generate the flat file, use the bencode() function.
		// 
		// It may be useful to add custom entries to the torrent file before bencoding it
		// and saving it to disk.
		// 
		// If anything goes wrong during torrent generation, this function will return
		// an empty ``entry`` structure. You can test for this condition by querying the
		// type of the entry:
		//
		// .. code:: c++
		// 
		//	file_storage fs;
		//	// add file ...
		//	create_torrent t(fs);
		//	// add trackers and piece hashes ...
		//	e = t.generate();
		//
		//	if (e.type() == entry::undefined_t)
		//	{
		//		// something went wrong
		//	}
		// 
		// For instance, you cannot generate a torrent with 0 files in it. If you don't add
		// any files to the ``file_storage``, torrent generation will fail.
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

		// This sets the SHA-1 hash for the specified piece (``index``). You are required
		// to set the hash for every piece in the torrent before generating it. If you have
		// the files on disk, you can use the high level convenience function to do this.
		// See set_piece_hashes().
		void set_hash(int index, sha1_hash const& h);

		// This sets the sha1 hash for this file. This hash will end up under the key ``sha1``
		// associated with this file (for multi-file torrents) or in the root info dictionary
		// for single-file torrents.
		void set_file_hash(int index, sha1_hash const& h);

		// This adds a url seed to the torrent. You can have any number of url seeds. For a
		// single file torrent, this should be an HTTP url, pointing to a file with identical
		// content as the file of the torrent. For a multi-file torrent, it should point to
		// a directory containing a directory with the same name as this torrent, and all the
		// files of the torrent in it.
		// 
		// The second function, ``add_http_seed()`` adds an HTTP seed instead.
		void add_url_seed(std::string const& url);
		void add_http_seed(std::string const& url);

		// This adds a DHT node to the torrent. This especially useful if you're creating a
		// tracker less torrent. It can be used by clients to bootstrap their DHT node from.
		// The node is a hostname and a port number where there is a DHT node running.
		// You can have any number of DHT nodes in a torrent.
		void add_node(std::pair<std::string, int> const& node);

		// Adds a tracker to the torrent. This is not strictly required, but most torrents
		// use a tracker as their main source of peers. The url should be an http:// or udp://
		// url to a machine running a bittorrent tracker that accepts announces for this torrent's
		// info-hash. The tier is the fallback priority of the tracker. All trackers with tier 0 are
		// tried first (in any order). If all fail, trackers with tier 1 are tried. If all of those
		// fail, trackers with tier 2 are tried, and so on.
		void add_tracker(std::string const& url, int tier = 0);

		// This function sets an X.509 certificate in PEM format to the torrent. This makes the
		// torrent an *SSL torrent*. An SSL torrent requires that each peer has a valid certificate
		// signed by this root certificate. For SSL torrents, all peers are connecting over SSL
		// connections. For more information, see the section on ssl-torrents_.
		// 
		// The string is not the path to the cert, it's the actual content of the certificate,
		// loaded into a std::string.
		void set_root_cert(std::string const& pem);

		// Sets and queries the private flag of the torrent.
		// Torrents with the private flag set ask clients to not use any other
		// sources than the tracker for peers, and to not advertize itself publicly,
		// apart from the tracker.
		void set_priv(bool p) { m_private = p; }
		bool priv() const { return m_private; }

		// returns the number of pieces in the associated file_storage object.
		int num_pieces() const { return m_files.num_pieces(); }

		// ``piece_length()`` returns the piece size of all pieces but the
		// last one. ``piece_size()`` returns the size of the specified piece.
		// these functions are just forwarding to the associated file_storage.
		int piece_length() const { return m_files.piece_length(); }
		int piece_size(int i) const { return m_files.piece_size(i); }

		// internal
		bool should_add_file_hashes() const { return m_calculate_file_hashes; }

		// This function returns the merkle hash tree, if the torrent was created as a merkle
		// torrent. The tree is created by ``generate()`` and won't be valid until that function
		// has been called. When creating a merkle tree torrent, the actual tree itself has to
		// be saved off separately and fed into libtorrent the first time you start seeding it,
		// through the ``torrent_info::set_merkle_tree()`` function. From that point onwards, the
		// tree will be saved in the resume data.
		std::vector<sha1_hash> const& merkle_tree() const { return m_merkle_tree; }

	private:

		file_storage& m_files;
		// if m_info_dict is initialized, it is 
		// used instead of m_files to generate
		// the info dictionary
		entry m_info_dict;

		// the urls to the trackers
		typedef std::pair<std::string, int> announce_entry;
		std::vector<announce_entry> m_urls;

		std::vector<std::string> m_url_seeds;
		std::vector<std::string> m_http_seeds;

		std::vector<sha1_hash> m_piece_hash;

		std::vector<sha1_hash> m_filehashes;

		// if we're generating a merkle torrent, this is the
		// merkle tree we got. This should be saved in fast-resume
		// in order to start seeding the torrent
		mutable std::vector<sha1_hash> m_merkle_tree;

		// dht nodes to add to the routing table/bootstrap from
		typedef std::vector<std::pair<std::string, int> > nodes_t;
		nodes_t m_nodes;

		// the hash that identifies this torrent
		// is mutable because it's calculated
		// lazily
		mutable sha1_hash m_info_hash;

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
		// to know if it should be written as a multifile torrent
		// or not. e.g. test/test  there's one file and one directory
		// and they have the same name.
		bool m_multifile:1;
		
		// this is true if the torrent is private. i.e., is should not
		// be announced on the dht
		bool m_private:1;

		// if set to one, a merkle torrent will be generated
		bool m_merkle_torrent:1;

		// if set, include the 'mtime' modification time in the
		// torrent file
		bool m_include_mtime:1;

		// if set, symbolic links are declared as such in
		// the torrent file. The full data of the pointed-to
		// file is still included
		bool m_include_symlinks:1;

		// this is only used by set_piece_hashes(). It will
		// calculate sha1 hashes for each file and add it
		// to the file list
		bool m_calculate_file_hashes:1;
	};

	namespace detail
	{
		inline bool default_pred(std::string const&) { return true; }

		inline bool ignore_subdir(std::string const& leaf)
		{ return leaf == ".." || leaf == "."; }

		inline void nop(int) {}

		int get_file_attributes(std::string const& p);
		std::string get_symlink_path(std::string const& p);

		// internal
		TORRENT_EXPORT void add_files_impl(file_storage& fs, std::string const& p
			, std::string const& l, boost::function<bool(std::string)> pred
			, boost::uint32_t flags);
	}

	// Adds the file specified by ``path`` to the file_storage object. In case ``path``
	// refers to a diretory, files will be added recursively from the directory.
	// 
	// If specified, the predicate ``p`` is called once for every file and directory that
	// is encountered. files for which ``p`` returns true are added, and directories for
	// which ``p`` returns true are traversed. ``p`` must have the following signature::
	// 
	// 	bool Pred(std::string const& p);
	// 
	// The path that is passed in to the predicate is the full path of the file or
	// directory. If no predicate is specified, all files are added, and all directories
	// are traveresed.
	// 
	// The ".." directory is never traversed.
	// 
	// The ``flags`` argument should be the same as the flags passed to the `create_torrent`_
	// constructor.
	template <class Pred> void add_files(file_storage& fs, std::string const& file, Pred p, boost::uint32_t flags = 0)
	{
		detail::add_files_impl(fs, parent_path(complete(file)), filename(file), p, flags);
	}
	inline void add_files(file_storage& fs, std::string const& file, boost::uint32_t flags = 0)
	{
		detail::add_files_impl(fs, parent_path(complete(file)), filename(file)
			, detail::default_pred, flags);
	}
	
	// This function will assume that the files added to the torrent file exists at path
	// ``p``, read those files and hash the content and set the hashes in the ``create_torrent``
	// object. The optional function ``f`` is called in between every hash that is set. ``f``
	// must have the following signature::
	// 
	// 	void Fun(int);
	// 
	// The overloads that don't take an ``error_code&`` may throw an exception in case of a
	// file error, the other overloads sets the error code to reflect the error, if any.
	TORRENT_EXPORT void set_piece_hashes(create_torrent& t, std::string const& p
		, boost::function<void(int)> f, error_code& ec);
	inline void set_piece_hashes(create_torrent& t, std::string const& p, error_code& ec)
	{
		set_piece_hashes(t, p, detail::nop, ec);
	}
#ifndef BOOST_NO_EXCEPTIONS
	inline void set_piece_hashes(create_torrent& t, std::string const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, detail::nop, ec);
		if (ec) throw libtorrent_exception(ec);
	}
	template <class Fun>
	void set_piece_hashes(create_torrent& t, std::string const& p, Fun f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) throw libtorrent_exception(ec);
	}
#endif

#if TORRENT_USE_WSTRING
	// wstring versions

	// all wstring APIs are deprecated since 0.16.11
	// instead, use the wchar -> utf8 conversion functions
	// and pass in utf8 strings
#ifndef TORRENT_NO_DEPRECATE

	template <class Pred>
	TORRENT_DEPRECATED_PREFIX
	void TORRENT_DEPRECATED add_files(file_storage& fs, std::wstring const& wfile, Pred p, boost::uint32_t flags = 0)
	{
		std::string utf8;
		wchar_utf8(wfile, utf8);
		detail::add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), p, flags);
	}

	TORRENT_DEPRECATED_PREFIX
	inline void TORRENT_DEPRECATED add_files(file_storage& fs, std::wstring const& wfile, boost::uint32_t flags = 0)
	{
		std::string utf8;
		wchar_utf8(wfile, utf8);
		detail::add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), detail::default_pred, flags);
	}
	
	void TORRENT_EXPORT set_piece_hashes(create_torrent& t, std::wstring const& p
		, boost::function<void(int)> const& f, error_code& ec);

#ifndef BOOST_NO_EXCEPTIONS
	template <class Fun>
	TORRENT_DEPRECATED_PREFIX
	void TORRENT_DEPRECATED set_piece_hashes(create_torrent& t, std::wstring const& p, Fun f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) throw libtorrent_exception(ec);
	}

	TORRENT_DEPRECATED_PREFIX
	inline void TORRENT_DEPRECATED set_piece_hashes(create_torrent& t, std::wstring const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, detail::nop, ec);
		if (ec) throw libtorrent_exception(ec);
	}
#endif

	TORRENT_DEPRECATED_PREFIX
	inline void TORRENT_DEPRECATED set_piece_hashes(create_torrent& t, std::wstring const& p, error_code& ec)
	{
		set_piece_hashes(t, p, detail::nop, ec);
	} 
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

}

#endif

