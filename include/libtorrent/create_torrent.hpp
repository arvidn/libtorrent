/*

Copyright (c) 2008, Arvid Norberg
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

namespace libtorrent
{
	class torrent_info;

	struct TORRENT_EXPORT create_torrent
	{
		enum flags_t
		{
			optimize = 1
			, merkle = 2
			, modification_time = 4
			, symlinks = 8
			, calculate_file_hashes = 16
		};

		create_torrent(file_storage& fs, int piece_size = 0
			, int pad_file_limit = -1, int flags = optimize);
		create_torrent(torrent_info const& ti);
		~create_torrent();
		entry generate() const;

		file_storage const& files() const { return m_files; }

		void set_comment(char const* str);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void set_file_hash(int index, sha1_hash const& h);
		void add_url_seed(std::string const& url);
		void add_http_seed(std::string const& url);
		void add_node(std::pair<std::string, int> const& node);
		void add_tracker(std::string const& url, int tier = 0);
		void set_root_cert(std::string const& pem);
		void set_priv(bool p) { m_private = p; }

		int num_pieces() const { return m_files.num_pieces(); }
		int piece_length() const { return m_files.piece_length(); }
		int piece_size(int i) const { return m_files.piece_size(i); }
		bool priv() const { return m_private; }

		bool should_add_file_hashes() const { return m_calculate_file_hashes; }
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

		inline void nop(int i) {}

		int get_file_attributes(std::string const& p);
		std::string get_symlink_path(std::string const& p);

		TORRENT_EXPORT void add_files_impl(file_storage& fs, std::string const& p
			, std::string const& l, boost::function<bool(std::string)> pred
			, boost::uint32_t flags);
	}

	template <class Pred>
	void add_files(file_storage& fs, std::string const& file, Pred p, boost::uint32_t flags = 0)
	{
		detail::add_files_impl(fs, parent_path(complete(file)), filename(file), p, flags);
	}

	inline void add_files(file_storage& fs, std::string const& file, boost::uint32_t flags = 0)
	{
		detail::add_files_impl(fs, parent_path(complete(file)), filename(file)
			, detail::default_pred, flags);
	}
	
	TORRENT_EXPORT void set_piece_hashes(create_torrent& t, std::string const& p
		, boost::function<void(int)> f, error_code& ec);

#ifndef BOOST_NO_EXCEPTIONS
	template <class Fun>
	void set_piece_hashes(create_torrent& t, std::string const& p, Fun f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) throw libtorrent_exception(ec);
	}

	inline void set_piece_hashes(create_torrent& t, std::string const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, detail::nop, ec);
		if (ec) throw libtorrent_exception(ec);
	}
#endif

	inline void set_piece_hashes(create_torrent& t, std::string const& p, error_code& ec)
	{
		set_piece_hashes(t, p, detail::nop, ec);
	}

#if TORRENT_USE_WSTRING
	// wstring versions

	template <class Pred>
	void add_files(file_storage& fs, std::wstring const& wfile, Pred p, boost::uint32_t flags = 0)
	{
		std::string utf8;
		wchar_utf8(wfile, utf8);
		detail::add_files_impl(fs, parent_path(complete(utf8))
			, filename(utf8), p, flags);
	}

	inline void add_files(file_storage& fs, std::wstring const& wfile, boost::uint32_t flags = 0)
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
	void set_piece_hashes(create_torrent& t, std::wstring const& p, Fun f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) throw libtorrent_exception(ec);
	}

	inline void set_piece_hashes(create_torrent& t, std::wstring const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, detail::nop, ec);
		if (ec) throw libtorrent_exception(ec);
	}
#endif

	inline void set_piece_hashes(create_torrent& t, std::wstring const& p, error_code& ec)
	{
		set_piece_hashes(t, p, detail::nop, ec);
	}

#endif // TORRENT_USE_WPATH

}

#endif

