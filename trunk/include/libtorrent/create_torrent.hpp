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
#include "libtorrent/file_pool.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/allocator.hpp"

#include <vector>
#include <string>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/optional.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace libtorrent
{
	namespace fs = boost::filesystem;
	namespace pt = boost::posix_time;
	class torrent_info;

	struct TORRENT_EXPORT create_torrent
	{
		enum { optimize = 1, merkle = 2 };

		create_torrent(file_storage& fs, int piece_size = 0
			, int pad_file_limit = -1, int flags = optimize);
		create_torrent(torrent_info const& ti);
		entry generate() const;

		file_storage const& files() const { return m_files; }

		void set_comment(char const* str);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void add_url_seed(std::string const& url);
		void add_node(std::pair<std::string, int> const& node);
		void add_tracker(std::string const& url, int tier = 0);
		void set_priv(bool p) { m_private = p; }

		int num_pieces() const { return m_files.num_pieces(); }
		int piece_length() const { return m_files.piece_length(); }
		int piece_size(int i) const { return m_files.piece_size(i); }
		bool priv() const { return m_private; }

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

		std::vector<sha1_hash> m_piece_hash;

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
		pt::ptime m_creation_date;

		// if a comment is found in the torrent file
		// this will be set to that comment
		std::string m_comment;

		// an optional string naming the software used
		// to create the torrent file
		std::string m_created_by;

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
	};

	namespace detail
	{
		inline bool default_pred(boost::filesystem::path const&) { return true; }
		inline bool wdefault_pred(boost::filesystem::wpath const&) { return true; }

		inline bool ignore_subdir(std::string const& leaf)
		{ return leaf == ".." || leaf == "."; }

		inline bool ignore_subdir(std::wstring const& leaf)
		{ return leaf == L".." || leaf == L"."; }

		inline void nop(int i) {}

		int TORRENT_EXPORT get_file_attributes(boost::filesystem::path const& p);
		int TORRENT_EXPORT get_file_attributes(boost::filesystem::wpath const& p);

		template <class Pred, class Str, class PathTraits>
		void add_files_impl(file_storage& fs, boost::filesystem::basic_path<Str, PathTraits> const& p
			, boost::filesystem::basic_path<Str, PathTraits> const& l, Pred pred)
		{
			using boost::filesystem::basic_path;
			using boost::filesystem::basic_directory_iterator;
#if BOOST_VERSION < 103600
			Str const& leaf = l.leaf();
#else
			Str const& leaf = l.filename();
#endif
			if (ignore_subdir(leaf)) return;
			if (!pred(l)) return;
			basic_path<Str, PathTraits> f(p / l);
			if (is_directory(f))
			{
				for (basic_directory_iterator<basic_path<Str, PathTraits> > i(f), end; i != end; ++i)
#if BOOST_VERSION < 103600
					add_files_impl(fs, p, l / i->path().leaf(), pred);
#else
					add_files_impl(fs, p, l / i->path().filename(), pred);
#endif
			}
			else
			{
				int file_flags = get_file_attributes(f);
				fs.add_file(l, file_size(f), file_flags);
			}
		}
	}

	// path versions

	template <class Pred>
	void add_files(file_storage& fs, boost::filesystem::path const& file, Pred p)
	{
		using boost::filesystem::path;
#if BOOST_VERSION < 103600
		detail::add_files_impl(fs, complete(file).branch_path(), path(file.leaf()), p);
#else
		detail::add_files_impl(fs, complete(file).parent_path(), path(file.filename()), p);
#endif
	}

	inline void add_files(file_storage& fs, boost::filesystem::path const& file)
	{
		using boost::filesystem::path;
#if BOOST_VERSION < 103600
		detail::add_files_impl(fs, complete(file).branch_path(), path(file.leaf()), detail::default_pred);
#else
		detail::add_files_impl(fs, complete(file).parent_path(), path(file.filename()), detail::default_pred);
#endif
	}
	
	struct piece_holder
	{
		piece_holder(int bytes): m_piece(page_aligned_allocator::malloc(bytes)) {}
		~piece_holder() { page_aligned_allocator::free(m_piece); }
		char* bytes() { return m_piece; }
	private:
		char* m_piece;
	};

	template <class Fun>
	void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p, Fun f
		, error_code& ec)
	{
		file_pool fp;
		boost::scoped_ptr<storage_interface> st(
			default_storage_constructor(const_cast<file_storage&>(t.files()), p, fp));

		// calculate the hash for all pieces
		int num = t.num_pieces();
		piece_holder buf(t.piece_length());
		for (int i = 0; i < num; ++i)
		{
			// read hits the disk and will block. Progress should
			// be updated in between reads
			st->read(buf.bytes(), i, 0, t.piece_size(i));
			if (st->error())
			{
				ec = st->error();
				return;
			}
			hasher h(buf.bytes(), t.piece_size(i));
			t.set_hash(i, h.final());
			f(i);
		}
	}

	template <class Fun>
	void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p, Fun f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) throw libtorrent_exception(ec);
	}

#ifndef BOOST_NOEXCEPTIONS
	inline void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, detail::nop, ec);
		if (ec) throw libtorrent_exception(ec);
	}

	inline void set_piece_hashes(create_torrent& t, boost::filesystem::path const& p, error_code& ec)
	{
		set_piece_hashes(t, p, detail::nop, ec);
	}
#endif

	// wpath versions

	template <class Pred>
	void add_files(file_storage& fs, boost::filesystem::wpath const& file, Pred p)
	{
		using boost::filesystem::wpath;
#if BOOST_VERSION < 103600
		detail::add_files_impl(fs, complete(file).branch_path(), wpath(file.leaf()), p);
#else
		detail::add_files_impl(fs, complete(file).parent_path(), wpath(file.filename()), p);
#endif
	}

	inline void add_files(file_storage& fs, boost::filesystem::wpath const& file)
	{
		using boost::filesystem::wpath;
#if BOOST_VERSION < 103600
		detail::add_files_impl(fs, complete(file).branch_path(), wpath(file.leaf()), detail::wdefault_pred);
#else
		detail::add_files_impl(fs, complete(file).parent_path(), wpath(file.filename()), detail::wdefault_pred);
#endif
	}
	
	template <class Fun>
	void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p, Fun f
		, error_code& ec)
	{
		file_pool fp;
		std::string utf8;
		wchar_utf8(p.string(), utf8);
		boost::scoped_ptr<storage_interface> st(
			default_storage_constructor(const_cast<file_storage&>(t.files()), utf8, fp));

		// calculate the hash for all pieces
		int num = t.num_pieces();
		std::vector<char> buf(t.piece_length());
		for (int i = 0; i < num; ++i)
		{
			// read hits the disk and will block. Progress should
			// be updated in between reads
			st->read(&buf[0], i, 0, t.piece_size(i));
			if (st->error())
			{
				ec = st->error();
				return;
			}
			hasher h(&buf[0], t.piece_size(i));
			t.set_hash(i, h.final());
			f(i);
		}
	}

#ifndef BOOST_NOEXCEPTIONS
	template <class Fun>
	void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p, Fun f)
	{
		error_code ec;
		set_piece_hashes(t, p, f, ec);
		if (ec) throw libtorrent_exception(ec);
	}

	inline void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p)
	{
		error_code ec;
		set_piece_hashes(t, p, detail::nop, ec);
		if (ec) throw libtorrent_exception(ec);
	}
#endif

	inline void set_piece_hashes(create_torrent& t, boost::filesystem::wpath const& p, error_code& ec)
	{
		set_piece_hashes(t, p, detail::nop, ec);
	}
}

#endif

