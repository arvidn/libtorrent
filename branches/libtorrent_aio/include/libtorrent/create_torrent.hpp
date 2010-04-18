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
#include "libtorrent/file.hpp" // for combine_path etc.

#include <vector>
#include <string>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/date_time/posix_time/ptime.hpp>
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
		enum {
			optimize = 1
			, merkle = 2
			, modification_time = 4
			, symlinks = 8
			, calculate_file_hashes = 16
		};

		create_torrent(file_storage& fs, int piece_size = 0
			, int pad_file_limit = -1, int flags = optimize);
		create_torrent(torrent_info const& ti);
		entry generate() const;

		file_storage const& files() const { return m_files; }

		void set_comment(char const* str);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void set_file_hash(int index, sha1_hash const& h);
		void add_url_seed(std::string const& url);
		void add_node(std::pair<std::string, int> const& node);
		void add_tracker(std::string const& url, int tier = 0);
		void set_priv(bool p) { m_private = p; }

		int num_pieces() const { return m_files.num_pieces(); }
		int piece_length() const { return m_files.piece_length(); }
		int piece_size(int i) const { return m_files.piece_size(i); }
		bool priv() const { return m_private; }

		bool should_add_file_hashes() const { return m_calculate_file_hashes; }

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

		std::vector<sha1_hash> m_filehashes;

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
		boost::posix_time::ptime m_creation_date;

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

		int TORRENT_EXPORT get_file_attributes(std::string const& p);
		std::string TORRENT_EXPORT get_symlink_path(std::string const& p);

		template <class Pred>
		void add_files_impl(file_storage& fs, std::string const& p
			, std::string const& l, Pred pred, boost::uint32_t flags)
		{
			std::string f = combine_path(p, l);
			if (!pred(f)) return;
			error_code ec;
			file_status s;
			stat_file(f, &s, ec, (flags & create_torrent::symlinks) ? dont_follow_links : 0);
			if (ec) return;

			// recurse into directories
			bool recurse = s.mode & file_status::directory;

			// if the file is not a link or we're following links, and it's a directory
			// only then should we recurse
#ifndef TORRENT_WINDOWS
			if ((s.mode & file_status::link) && (flags & create_torrent::symlinks))
				recurse = false;
#endif

			if (recurse)
			{
				for (directory i(f, ec); !i.done(); i.next(ec))
				{
					std::string leaf = i.file();
					if (ignore_subdir(leaf)) continue;
					add_files_impl(fs, p, combine_path(l, leaf), pred, flags);
				}
			}
			else
			{
				// #error use the fields from s
				int file_flags = get_file_attributes(f);

				// mask all bits to check if the file is a symlink
				if ((file_flags & file_storage::attribute_symlink)
					&& (flags & create_torrent::symlinks)) 
				{
					std::string sym_path = get_symlink_path(f);
					fs.add_file(l, 0, file_flags, s.mtime, sym_path);
				}
				else
				{
					fs.add_file(l, s.file_size, file_flags, s.mtime);
				}
			}
		}
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
	
	struct piece_holder
	{
		piece_holder(int bytes): m_piece(page_aligned_allocator::malloc(bytes)) {}
		~piece_holder() { page_aligned_allocator::free(m_piece); }
		char* bytes() { return m_piece; }
	private:
		char* m_piece;
	};

	template <class Fun>
	void set_piece_hashes(create_torrent& t, std::string const& p, Fun f
		, error_code& ec)
	{
		file_pool fp;
		boost::scoped_ptr<storage_interface> st(
			default_storage_constructor(const_cast<file_storage&>(t.files()), 0, p, fp));

		// if we're calculating file hashes as well, use this hasher
		hasher filehash;
		int file_idx = 0;
		size_type left_in_file = t.files().at(0).size;

		// calculate the hash for all pieces
		int num = t.num_pieces();
		piece_holder buf(t.piece_length());
		for (int i = 0; i < num; ++i)
		{
			// read hits the disk and will block. Progress should
			// be updated in between reads
			st->read(buf.bytes(), i, 0, t.piece_size(i), ec);
			if (ec) return;
			
			if (t.should_add_file_hashes())
			{
				int left_in_piece = t.piece_size(i);
				// the number of bytes from this file we just read
				while (left_in_piece > 0)
				{
					int to_hash_for_file = (std::min)(size_type(left_in_piece), left_in_file);
					filehash.update(buf.bytes(), to_hash_for_file);
					left_in_file -= to_hash_for_file;
					left_in_piece -= to_hash_for_file;
					if (left_in_file == 0)
					{
						if (!t.files().at(file_idx).pad_file)
							t.set_file_hash(file_idx, filehash.final());
						filehash.reset();
						file_idx++;
						if (file_idx >= t.files().num_files()) break;
						left_in_file = t.files().at(file_idx).size;
					}
				}
			}

			hasher h(buf.bytes(), t.piece_size(i));
			t.set_hash(i, h.final());
			f(i);
		}
	}

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
	
	template <class Fun>
	void set_piece_hashes(create_torrent& t, std::wstring const& p, Fun f
		, error_code& ec)
	{
		file_pool fp;
		std::string utf8;
		wchar_utf8(p, utf8);
		boost::scoped_ptr<storage_interface> st(
			default_storage_constructor(const_cast<file_storage&>(t.files()), 0, utf8, fp));

		// calculate the hash for all pieces
		int num = t.num_pieces();
		std::vector<char> buf(t.piece_length());
		for (int i = 0; i < num; ++i)
		{
			// read hits the disk and will block. Progress should
			// be updated in between reads
			st->read(&buf[0], i, 0, t.piece_size(i), ec);
			if (ec) return;
			hasher h(&buf[0], t.piece_size(i));
			t.set_hash(i, h.final());
			f(i);
		}
	}

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

