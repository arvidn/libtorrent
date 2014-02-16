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

#include "libtorrent/size_type.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/peer_id.hpp"

namespace libtorrent
{
	struct file;

	struct TORRENT_EXPORT file_entry
	{
		file_entry();
		~file_entry();

		std::string path;
		size_type offset; // the offset of this file inside the torrent
		size_type size; // the size of this file
		// the offset in the file where the storage starts.
		// This is always 0 unless parts of the torrent is
		// compressed into a single file, such as a so-called part file.
		size_type file_base;
		std::time_t mtime;
		sha1_hash filehash;
		bool pad_file:1;
		bool hidden_attribute:1;
		bool executable_attribute:1;
		bool symlink_attribute:1;
		std::string symlink_path;
	};

	// this is used internally to hold the file entry
	// it's smaller and optimized for smaller memory
	// footprint, as opposed to file_entry, which is
	// optimized for convenience
	struct TORRENT_EXPORT internal_file_entry
	{
		friend class file_storage;
#ifdef TORRENT_DEBUG
		// for torrent_info::invariant_check
		friend class torrent_info;
#endif
		internal_file_entry()
			: name(0)
			, offset(0)
			, symlink_index(-1)
			, size(0)
			, name_len(0)
			, pad_file(false)
			, hidden_attribute(false)
			, executable_attribute(false)
			, symlink_attribute(false)
			, no_root_dir(false)
			, path_index(-1)
		{}

		internal_file_entry(file_entry const& e)
			: name(0)
			, offset(e.offset)
			, symlink_index(-1)
			, size(e.size)
			, name_len(0)
			, pad_file(e.pad_file)
			, hidden_attribute(e.hidden_attribute)
			, executable_attribute(e.executable_attribute)
			, symlink_attribute(e.symlink_attribute)
			, no_root_dir(false)
			, path_index(-1)
		{
			set_name(e.path.c_str());
		}

		internal_file_entry(internal_file_entry const& fe);
		internal_file_entry& operator=(internal_file_entry const& fe);

		~internal_file_entry();

		void set_name(char const* n, int borrow_chars = 0);
		std::string filename() const;

		// make it available for logging
#if !defined TORRENT_VERBOSE_LOGGING \
	&& !defined TORRENT_LOGGING \
	&& !defined TORRENT_ERROR_LOGGING
	private:
#endif
		// This string is not necessarily null terminated!
		// that's why it's private, to keep people away from it
		char const* name;
	public:

		// the offset of this file inside the torrent
		size_type offset:48;

		// index into file_storage::m_symlinks or -1
		// if this is not a symlink
		size_type symlink_index:16;

		// the size of this file
		size_type size:48;

		// the number of characters in the name. If this is
		// 0, name is null terminated and owned by this object
		// (i.e. it should be freed in the destructor). If
		// the len is > 0, the name pointer doesn not belong
		// to this object, and it's not null terminated
		size_type name_len:10;
		bool pad_file:1;
		bool hidden_attribute:1;
		bool executable_attribute:1;
		bool symlink_attribute:1;

		// if this is true, don't include m_name as part of the
		// path to this file
		boost::uint64_t no_root_dir:1;

		// the index into file_storage::m_paths. To get
		// the full path to this file, concatenate the path
		// from that array with the 'name' field in
		// this struct
		// values for path_index include:
		// -1 means no path (i.e. single file torrent)
		int path_index;
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

		void reserve(int num_files);

		void add_file(file_entry const& e, char const* filehash = 0);

		void add_file(std::string const& p, size_type size, int flags = 0
			, std::time_t mtime = 0, std::string const& s_p = "");

		void rename_file(int index, std::string const& new_filename);

#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11
		// instead, use the wchar -> utf8 conversion functions
		// and pass in utf8 strings
#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		void add_file(std::wstring const& p, size_type size, int flags = 0
			, std::time_t mtime = 0, std::string const& s_p = "") TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void rename_file(int index, std::wstring const& new_filename) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_name(std::wstring const& n) TORRENT_DEPRECATED;
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

		std::vector<file_slice> map_block(int piece, size_type offset
			, int size) const;
		peer_request map_file(int file, size_type offset, int size) const;
		
		typedef std::vector<internal_file_entry>::const_iterator iterator;
		typedef std::vector<internal_file_entry>::const_reverse_iterator reverse_iterator;

		iterator file_at_offset(size_type offset) const;
		iterator begin() const { return m_files.begin(); }
		iterator end() const { return m_files.end(); }
		reverse_iterator rbegin() const { return m_files.rbegin(); }
		reverse_iterator rend() const { return m_files.rend(); }
		int num_files() const
		{ return int(m_files.size()); }

		file_entry at(int index) const;
		file_entry at(iterator i) const;
		internal_file_entry const& internal_at(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < int(m_files.size()));
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
			swap(ti.m_files, m_files);
			swap(ti.m_file_hashes, m_file_hashes);
			swap(ti.m_symlinks, m_symlinks);
			swap(ti.m_mtime, m_mtime);
			swap(ti.m_file_base, m_file_base);
			swap(ti.m_paths, m_paths);
			swap(ti.m_name, m_name);
			swap(ti.m_total_size, m_total_size);
			swap(ti.m_num_pieces, m_num_pieces);
			swap(ti.m_piece_length, m_piece_length);
		}

		// if pad_file_limit >= 0, files larger than
		// that limit will be padded, default is to
		// not add any padding
		void optimize(int pad_file_limit = -1);

		sha1_hash hash(int index) const;
		std::string const& symlink(int index) const;
		time_t mtime(int index) const;
		int file_index(int index) const;
		size_type file_base(int index) const;
		void set_file_base(int index, size_type off);
		std::string file_path(int index) const;
		size_type file_size(int index) const;
		std::string file_name(int index) const;

		sha1_hash hash(internal_file_entry const& fe) const;
		std::string const& symlink(internal_file_entry const& fe) const;
		time_t mtime(internal_file_entry const& fe) const;
		int file_index(internal_file_entry const& fe) const;
		size_type file_base(internal_file_entry const& fe) const;
		void set_file_base(internal_file_entry const& fe, size_type off);
		std::string file_path(internal_file_entry const& fe) const;
		size_type file_size(internal_file_entry const& fe) const;

#if !defined TORRENT_VERBOSE_LOGGING \
	&& !defined TORRENT_LOGGING \
	&& !defined TORRENT_ERROR_LOGGING
	private:
#endif

		void update_path_index(internal_file_entry& e);
		void reorder_file(int index, int dst);

		// the list of files that this torrent consists of
		std::vector<internal_file_entry> m_files;

		// if there are sha1 hashes for each individual file
		// there are as many entries in this array as the
		// m_files array. Each entry in m_files has a corresponding
		// hash pointer in this array. The reason to split it up
		// in separate arrays is to save memory in case the torrent
		// doesn't have file hashes
		std::vector<char const*> m_file_hashes;

		// for files that are symlinks, the symlink
		// path_index in the internal_file_entry indexes
		// this vector of strings
		std::vector<std::string> m_symlinks;

		// the modification times of each file. This vector
		// is empty if no file have a modification time.
		// each element corresponds to the file with the same
		// index in m_files
		std::vector<time_t> m_mtime;

		// if any file has a non-zero file base (i.e. multiple
		// files residing in the same physical file at different
		// offsets)
		std::vector<size_type> m_file_base;

		// all unique paths files have. The internal_file_entry::path_index
		// points into this array. The paths don't include the root directory
		// name for multi-file torrents. The m_name field need to be
		// prepended to these paths, and the filename of a specific file
		// entry appended, to form full file paths
		std::vector<std::string> m_paths;

		// name of torrent. For multi-file torrents
		// this is always the root directory
		std::string m_name;

		// the sum of all filesizes
		size_type m_total_size;

		// the number of pieces in the torrent
		int m_num_pieces;

		int m_piece_length;
	};
}

#endif // TORRENT_FILE_STORAGE_HPP_INCLUDED

