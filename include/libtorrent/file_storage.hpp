/*

Copyright (c) 2003-2008, Arvid Norberg
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
#include <ctime>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/path.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/size_type.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/peer_request.hpp"

namespace libtorrent
{
	namespace fs = boost::filesystem;

	struct TORRENT_EXPORT file_entry
	{
		file_entry(): offset(0), size(0), file_base(0)
			, mtime(0), pad_file(false), hidden_attribute(false)
			, executable_attribute(false)
			, symlink_attribute(false)
		{}

		fs::path path;
		size_type offset; // the offset of this file inside the torrent
		size_type size; // the size of this file
		// the offset in the file where the storage starts.
		// This is always 0 unless parts of the torrent is
		// compressed into a single file, such as a so-called part file.
		size_type file_base;
		std::time_t mtime;
		bool pad_file:1;
		bool hidden_attribute:1;
		bool executable_attribute:1;
		bool symlink_attribute:1;
		fs::path symlink_path;
	};

	struct TORRENT_EXPORT file_slice
	{
		int file_index;
		size_type offset;
		size_type size;
	};

	class TORRENT_EXPORT file_storage
	{
	friend class torrent_info;
	public:
		file_storage();
		~file_storage() {}

		bool is_valid() const { return m_piece_length > 0; }

		enum flags_t
		{
			pad_file = 1,
			attribute_hidden = 2,
			attribute_executable = 4,
			attribute_symlink = 8
		};

		void add_file(file_entry const& e);
		void add_file(fs::path const& p, size_type size, int flags = 0, std::time_t mtime = 0, fs::path const& s_p = "");
		void rename_file(int index, std::string const& new_filename);

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
		void add_file(fs::wpath const& p, size_type size, int flags = 0, std::time_t mtime = 0, fs::path const& s_p = "");
		void rename_file(int index, std::wstring const& new_filename);
		void set_name(std::wstring const& n);
#endif

		std::vector<file_slice> map_block(int piece, size_type offset
			, int size) const;
		peer_request map_file(int file, size_type offset, int size) const;
		
		typedef std::vector<file_entry>::const_iterator iterator;
		typedef std::vector<file_entry>::const_reverse_iterator reverse_iterator;

		iterator file_at_offset(size_type offset) const;
		iterator begin() const { return m_files.begin(); }
		iterator end() const { return m_files.end(); }
		reverse_iterator rbegin() const { return m_files.rbegin(); }
		reverse_iterator rend() const { return m_files.rend(); }
		int num_files() const
		{ return int(m_files.size()); }

		file_entry const& at(int index) const
		{
			TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
			return m_files[index];
		}
		
		size_type total_size() const { return m_total_size; }
		void set_num_pieces(int n) { m_num_pieces = n; }
		int num_pieces() const { TORRENT_ASSERT(m_piece_length > 0); return m_num_pieces; }
		void set_piece_length(int l)  { m_piece_length = l; }
		int piece_length() const { TORRENT_ASSERT(m_piece_length > 0); return m_piece_length; }
		int piece_size(int index) const;

		void set_name(std::string const& n) { m_name = n; }
		const std::string& name() const { return m_name; }

		void swap(file_storage& ti)
		{
			using std::swap;
			swap(ti.m_piece_length, m_piece_length);
			swap(ti.m_files, m_files);
			swap(ti.m_total_size, m_total_size);
			swap(ti.m_num_pieces, m_num_pieces);
			swap(ti.m_name, m_name);
		}

		// if pad_file_limit >= 0, files larger than
		// that limit will be padded, default is to
		// not add any padding
		void optimize(int pad_file_limit = -1);

	private:
		int m_piece_length;

		// the list of files that this torrent consists of
		std::vector<file_entry> m_files;

		// the sum of all filesizes
		size_type m_total_size;

		// the number of pieces in the torrent
		int m_num_pieces;
		std::string m_name;
	};
}

#endif // TORRENT_FILE_STORAGE_HPP_INCLUDED

