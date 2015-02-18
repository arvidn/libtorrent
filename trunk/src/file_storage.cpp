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

#include "libtorrent/file_storage.hpp"
#include "libtorrent/string_util.hpp" // for allocate_string_copy
#include "libtorrent/file.hpp"
#include "libtorrent/utf8.hpp"
#include <boost/bind.hpp>
#include <boost/crc.hpp>
#include <cstdio>
#include <algorithm>

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR '\\'
#else
#define TORRENT_SEPARATOR '/'
#endif

namespace libtorrent
{
	file_storage::file_storage()
		: m_piece_length(0)
		, m_num_pieces(0)
		, m_total_size(0)
		, m_num_files(0)
	{}

	file_storage::~file_storage() {}

	void file_storage::reserve(int num_files)
	{
		m_files.reserve(num_files);
	}

	int file_storage::piece_size(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			boost::int64_t size_except_last = num_pieces() - 1;
			size_except_last *= boost::int64_t(piece_length());
			boost::int64_t size = total_size() - size_except_last;
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

	namespace
	{
		bool compare_string(char const* str, int len, std::string const& str2)
		{
			if (str2.size() != len) return false;
			return memcmp(str2.c_str(), str, len) == 0;
		}
	}

	// path is not supposed to include the name of the torrent itself.
	void file_storage::update_path_index(internal_file_entry& e
		, std::string const& path, bool set_name)
	{
		if (is_complete(path))
		{
			TORRENT_ASSERT(set_name);
			e.set_name(path.c_str());
			e.path_index = -2;
			return;
		}

		TORRENT_ASSERT(path[0] != '/');

		// sorry about this messy string handling, but I did
		// profile it, and it was expensive
		char const* leaf = filename_cstr(path.c_str());
		char const* branch_path = "";
		int branch_len = 0;
		if (leaf > path.c_str())
		{
			// split the string into the leaf filename
			// and the branch path
			branch_path = path.c_str();
			branch_len = leaf - path.c_str();
		}
		if (branch_len <= 0)
		{
			if (set_name) e.set_name(leaf);
			e.path_index = -1;
			return;
		}

		if (branch_len >= m_name.size()
			&& std::memcmp(branch_path, m_name.c_str(), m_name.size()) == 0)
		{
			// the +1 is to skip the trailing '/' (or '\')
			int offset = m_name.size()
				+ (m_name.size() == branch_len?0:1);
			branch_path += offset;
			branch_len -= offset;
			e.no_root_dir = false;
		}
		else
		{
			e.no_root_dir = true;
		}

		// do we already have this path in the path list?
		std::vector<std::string>::reverse_iterator p
			= std::find_if(m_paths.rbegin(), m_paths.rend()
				, boost::bind(&compare_string, branch_path, branch_len, _1));

		if (p == m_paths.rend())
		{
			// no, we don't. add it
			e.path_index = m_paths.size();
			TORRENT_ASSERT(branch_path[0] != '/');

			// trim trailing slashes
			if (branch_len > 0 && branch_path[branch_len-1] == TORRENT_SEPARATOR)
				--branch_len;

			// poor man's emplace back
			m_paths.resize(m_paths.size() + 1);
			m_paths.back().assign(branch_path, branch_len);
		}
		else
		{
			// yes we do. use it
			e.path_index = p.base() - m_paths.begin() - 1;
		}
		if (set_name) e.set_name(leaf);
	}

#ifndef TORRENT_NO_DEPRECATE
	file_entry::file_entry(): offset(0), size(0), file_base(0)
		, mtime(0), pad_file(false), hidden_attribute(false)
		, executable_attribute(false)
		, symlink_attribute(false)
	{}

	file_entry::~file_entry() {}
#endif // TORRENT_NO_DEPRECATE

	internal_file_entry::~internal_file_entry()
	{
		if (name_len == name_is_owned) free((void*)name);
	}

	internal_file_entry::internal_file_entry(internal_file_entry const& fe)
		: offset(fe.offset)
		, symlink_index(fe.symlink_index)
		, no_root_dir(fe.no_root_dir)
		, size(fe.size)
		, name_len(fe.name_len)
		, pad_file(fe.pad_file)
		, hidden_attribute(fe.hidden_attribute)
		, executable_attribute(fe.executable_attribute)
		, symlink_attribute(fe.symlink_attribute)
		, name(0)
		, path_index(fe.path_index)
	{
		if (fe.name_len == name_is_owned)
			name = allocate_string_copy(fe.name);
		else
			name = fe.name;
	}

	internal_file_entry& internal_file_entry::operator=(internal_file_entry const& fe)
	{
		offset = fe.offset;
		size = fe.size;
		path_index = fe.path_index;
		symlink_index = fe.symlink_index;
		pad_file = fe.pad_file;
		hidden_attribute = fe.hidden_attribute;
		executable_attribute = fe.executable_attribute;
		symlink_attribute = fe.symlink_attribute;
		no_root_dir = fe.no_root_dir;
		set_name(fe.filename().c_str());
		return *this;
	}

	// if borrow_chars >= 0, don't take ownership over n, just
	// point to it. It points to borrow_chars number of characters.
	// if borrow_chars == -1, n is a null terminated string that
	// should be copied 
	void internal_file_entry::set_name(char const* n, bool borrow_string, int string_len)
	{
		TORRENT_ASSERT(string_len >= 0);

		// we have limited space in the length field. truncate string
		// if it's too long
		if (string_len >= name_is_owned) string_len = name_is_owned - 1;

		// free the current string, before assigning the new one
		if (name_len == name_is_owned) free((void*)name);
		if (n == NULL)
		{
			TORRENT_ASSERT(borrow_string == false);
			name = NULL;
		}
		else if (borrow_string)
		{
			name = n;
			name_len = string_len;
		}
		else
		{
			name = allocate_string_copy(n);
			name_len = name_is_owned;
		}
	}

	std::string internal_file_entry::filename() const
	{
		if (name_len != name_is_owned) return std::string(name, name_len);
		return name ? name : "";
	}

	void file_storage::apply_pointer_offset(ptrdiff_t off)
	{
		for (int i = 0; i < m_files.size(); ++i)
		{
			if (m_files[i].name_len == internal_file_entry::name_is_owned) continue;
			m_files[i].name += off;
		}

		for (int i = 0; i < m_file_hashes.size(); ++i)
		{
			if (m_file_hashes[i] == NULL) continue;
			m_file_hashes[i] += off;
		}
	}

#ifndef TORRENT_NO_DEPRECATE

	void file_storage::add_file(file_entry const& fe, char const* infohash)
	{
		int flags = 0;
		if (fe.pad_file) flags |= file_storage::flag_pad_file;
		if (fe.hidden_attribute) flags |= file_storage::flag_hidden;
		if (fe.executable_attribute) flags |= file_storage::flag_executable;
		if (fe.symlink_attribute) flags |= file_storage::flag_symlink;

		add_file_borrow(NULL, 0, fe.path, fe.size, flags, NULL, fe.mtime
			, fe.symlink_path);
	}

#if TORRENT_USE_WSTRING
	void file_storage::set_name(std::wstring const& n)
	{
		std::string utf8;
		wchar_utf8(n, utf8);
		m_name = utf8;
	}

	void file_storage::rename_file_deprecated(int index, std::wstring const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		std::string utf8;
		wchar_utf8(new_filename, utf8);
		update_path_index(m_files[index], utf8);
	}

	void file_storage::add_file(std::wstring const& file, boost::int64_t file_size
		, int file_flags, std::time_t mtime, std::string const& symlink_path)
	{
		std::string utf8;
		wchar_utf8(file, utf8);
		add_file(utf8, file_size, file_flags, mtime, symlink_path);
	}

	void file_storage::rename_file(int index, std::wstring const& new_filename)
	{
		rename_file_deprecated(index, new_filename);
	}
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE

	void file_storage::rename_file(int index, std::string const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		update_path_index(m_files[index], new_filename);
	}

	namespace
	{
		bool compare_file_offset(internal_file_entry const& lhs, internal_file_entry const& rhs)
		{
			return lhs.offset < rhs.offset;
		}
	}

#ifndef TORRENT_NO_DEPRECATE
	file_storage::iterator file_storage::file_at_offset_deprecated(boost::int64_t offset) const
	{
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = offset;
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<internal_file_entry>::const_iterator file_iter = std::upper_bound(
			begin_deprecated(), end_deprecated(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != begin_deprecated());
		--file_iter;
		return file_iter;
	}

	file_storage::iterator file_storage::file_at_offset(boost::int64_t offset) const
	{
		return file_at_offset_deprecated(offset);
	}
#endif

	int file_storage::file_index_at_offset(boost::int64_t offset) const
	{
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = offset;
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<internal_file_entry>::const_iterator file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;
		return file_iter - m_files.begin();
	}

	char const* file_storage::file_name_ptr(int index) const
	{
		return m_files[index].name;
	}

	int file_storage::file_name_len(int index) const
	{
		if (m_files[index].name_len == internal_file_entry::name_is_owned)
			return -1;
		return m_files[index].name_len;
	}

	std::vector<file_slice> file_storage::map_block(int piece, boost::int64_t offset
		, int size) const
	{
		TORRENT_ASSERT_PRECOND(num_files() > 0);
		std::vector<file_slice> ret;

		if (m_files.empty()) return ret;

		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = piece * (boost::int64_t)m_piece_length + offset;
		TORRENT_ASSERT_PRECOND(boost::int64_t(target.offset + size) <= m_total_size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<internal_file_entry>::const_iterator file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;

		boost::int64_t file_offset = target.offset - file_iter->offset;
		for (; size > 0; file_offset -= file_iter->size, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != m_files.end());
			if (file_offset < boost::int64_t(file_iter->size))
			{
				file_slice f;
				f.file_index = file_iter - m_files.begin();
				f.offset = file_offset
#ifndef TORRENT_NO_DEPRECATE
					+ file_base(f.file_index)
#endif
					;
				f.size = (std::min)(boost::uint64_t(file_iter->size) - file_offset, boost::uint64_t(size));
				TORRENT_ASSERT(f.size <= size);
				size -= int(f.size);
				file_offset += f.size;
				ret.push_back(f);
			}
			
			TORRENT_ASSERT(size >= 0);
		}
		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE
	file_entry file_storage::at(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		file_entry ret;
		internal_file_entry const& ife = m_files[index];
		ret.path = file_path(index);
		ret.offset = ife.offset;
		ret.size = ife.size;
		ret.file_base = file_base(index);
		ret.mtime = mtime(index);
		ret.pad_file = ife.pad_file;
		ret.hidden_attribute = ife.hidden_attribute;
		ret.executable_attribute = ife.executable_attribute;
		ret.symlink_attribute = ife.symlink_attribute;
		if (ife.symlink_index != internal_file_entry::not_a_symlink)
			ret.symlink_path = symlink(index);
		ret.filehash = hash(index);
		return ret;
	}
#endif // TORRENT_NO_DEPRECATE

	peer_request file_storage::map_file(int file_index, boost::int64_t file_offset
		, int size) const
	{
		TORRENT_ASSERT_PRECOND(file_index < num_files());
		TORRENT_ASSERT_PRECOND(file_index >= 0);
		TORRENT_ASSERT(m_num_pieces >= 0);

		peer_request ret;
		if (file_index < 0 || file_index >= num_files())
		{
			ret.piece = m_num_pieces;
			ret.start = 0;
			ret.length = 0;
			return ret;
		}

		boost::int64_t offset = file_offset + this->file_offset(file_index);

		if (offset >= total_size())
		{
			ret.piece = m_num_pieces;
			ret.start = 0;
			ret.length = 0;
		}
		else
		{
			ret.piece = int(offset / piece_length());
			ret.start = int(offset % piece_length());
			ret.length = size;
			if (offset + size > total_size())
				ret.length = int(total_size() - offset);
		}
		return ret;
	}

	void file_storage::add_file(std::string const& path, boost::int64_t file_size
		, int file_flags, std::time_t mtime, std::string const& symlink_path)
	{
		add_file_borrow(NULL, 0, path, file_size, file_flags, NULL, mtime
			, symlink_path);
	}

	void file_storage::add_file_borrow(char const* filename, int filename_len
		, std::string const& path, boost::int64_t file_size
		, boost::uint32_t file_flags, char const* filehash
		, boost::int64_t mtime, std::string const& symlink_path)
	{
		TORRENT_ASSERT_PRECOND(file_size >= 0);
		if (!has_parent_path(path))
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT_PRECOND(m_files.empty());
			m_name = path;
		}
		else
		{
			if (m_files.empty())
				m_name = split_path(path).c_str();
		}

		// this is poor-man's emplace_back()
		m_files.resize(m_files.size() + 1);
		internal_file_entry& e = m_files.back();

		// the last argument specified whether the function should also set
		// the filename. If it does, it will copy the leaf filename from path.
		// if filename is NULL, we should copy it. If it isn't, we're borrowing
		// it and we can save the copy by setting it after this call to
		// update_path_index().
		update_path_index(e, path, filename == NULL);

		// filename is allowed to be NULL, in which case we just use path
		if (filename)
			e.set_name(filename, true, filename_len);

		e.size = file_size;
		e.offset = m_total_size;
		e.pad_file = file_flags & file_storage::flag_pad_file;
		e.hidden_attribute = file_flags & file_storage::flag_hidden;
		e.executable_attribute = file_flags & file_storage::flag_executable;
		e.symlink_attribute = file_flags & file_storage::flag_symlink;

		if (filehash)
		{
			if (m_file_hashes.size() < m_files.size()) m_file_hashes.resize(m_files.size());
			m_file_hashes[m_files.size() - 1] = filehash;
		}
		if (!symlink_path.empty()
			&& m_symlinks.size() < internal_file_entry::not_a_symlink - 1)
		{
			e.symlink_index = m_symlinks.size();
			m_symlinks.push_back(symlink_path);
		}
		else
		{
			e.symlink_attribute = false;
		}
		if (mtime)
		{
			if (m_mtime.size() < m_files.size()) m_mtime.resize(m_files.size());
			m_mtime[m_files.size() - 1] = mtime;
		}

		++m_num_files;
		m_total_size += e.size;
	}

	sha1_hash file_storage::hash(int index) const
	{
		if (index >= int(m_file_hashes.size())) return sha1_hash(0);
		return sha1_hash(m_file_hashes[index]);
	}
	
	std::string const& file_storage::symlink(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];
		TORRENT_ASSERT(fe.symlink_index < int(m_symlinks.size()));
		return m_symlinks[fe.symlink_index];
	}

	time_t file_storage::mtime(int index) const
	{
		if (index >= int(m_mtime.size())) return 0;
		return m_mtime[index];
	}

	namespace
	{
		template <class CRC>
		void process_string_lowercase(CRC& crc, char const* str, int len)
		{
			for (int i = 0; i < len; ++i, ++str)
				crc.process_byte(to_lower(*str));
		}
	}

	boost::uint32_t file_storage::path_hash(int index
		, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_paths.size()));
		
		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;

		if (!save_path.empty())
		{
			process_string_lowercase(crc, save_path.c_str(), save_path.size());
			TORRENT_ASSERT(save_path[save_path.size()-1] != TORRENT_SEPARATOR);
			crc.process_byte(TORRENT_SEPARATOR);
		}

		process_string_lowercase(crc, m_name.c_str(), m_name.size());
		crc.process_byte(TORRENT_SEPARATOR);
		process_string_lowercase(crc, m_paths[index].c_str(), m_paths[index].size());
		return crc.checksum();
	}

	boost::uint32_t file_storage::file_path_hash(int index
		, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];

		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;

		if (fe.path_index == -2)
		{
			// -2 means this is an absolute path filename
			process_string_lowercase(crc, fe.filename_ptr(), fe.filename_len());
		}
		else if (fe.path_index == -1)
		{
			// -1 means no path
			if (!save_path.empty())
			{
				process_string_lowercase(crc, save_path.c_str(), save_path.size());
				TORRENT_ASSERT(save_path[save_path.size()-1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, fe.filename_ptr(), fe.filename_len());
		}
		else if (fe.no_root_dir)
		{
			if (!save_path.empty())
			{
				process_string_lowercase(crc, save_path.c_str(), save_path.size());
				TORRENT_ASSERT(save_path[save_path.size()-1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			std::string const& p = m_paths[fe.path_index];
			if (!p.empty())
			{
				process_string_lowercase(crc, p.c_str(), p.size());
				TORRENT_ASSERT(p[p.size()-1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, fe.filename_ptr(), fe.filename_len());
		}
		else
		{
			if (!save_path.empty())
			{
				process_string_lowercase(crc, save_path.c_str(), save_path.size());
				TORRENT_ASSERT(save_path[save_path.size()-1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, m_name.c_str(), m_name.size());
			TORRENT_ASSERT(m_name.size() > 0);
			TORRENT_ASSERT(m_name[m_name.size()-1] != TORRENT_SEPARATOR);
			crc.process_byte(TORRENT_SEPARATOR);

			std::string const& p = m_paths[fe.path_index];
			if (!p.empty())
			{
				process_string_lowercase(crc, p.c_str(), p.size());
				TORRENT_ASSERT(p.size() > 0);
				TORRENT_ASSERT(p[p.size()-1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, fe.filename_ptr(), fe.filename_len());
		}

		return crc.checksum();
	}

	std::string file_storage::file_path(int index, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];

		std::string ret;

		// -2 means this is an absolute path filename
		if (fe.path_index == -2)
		{
			ret.assign(fe.filename_ptr(), fe.filename_len());
		}
		else if (fe.path_index == -1)
		{
			// -1 means no path
			ret.reserve(save_path.size() + fe.filename_len() + 1);
			ret.assign(save_path);
			append_path(ret, fe.filename_ptr(), fe.filename_len());
		}
		else if (fe.no_root_dir)
		{
			std::string const& p = m_paths[fe.path_index];

			ret.reserve(save_path.size() + p.size() + fe.filename_len() + 2);
			ret.assign(save_path);
			append_path(ret, p);
			append_path(ret, fe.filename_ptr(), fe.filename_len());
		}
		else
		{
			std::string const& p = m_paths[fe.path_index];

			ret.reserve(save_path.size() + m_name.size() + p.size() + fe.filename_len() + 3);
			ret.assign(save_path);
			append_path(ret, m_name);
			append_path(ret, p);
			append_path(ret, fe.filename_ptr(), fe.filename_len());
		}

		// a single return statement, just to make NRVO more likely to kick in
		return ret;
	}

	std::string file_storage::file_name(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];
		return fe.filename();
	}

	boost::int64_t file_storage::file_size(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return m_files[index].size;
	}

	bool file_storage::pad_file_at(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return m_files[index].pad_file;
	}

	boost::int64_t file_storage::file_offset(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return m_files[index].offset;
	}

	int file_storage::file_flags(int index) const
	{
		internal_file_entry const& fe = m_files[index];
		return (fe.pad_file ? flag_pad_file : 0)
			| (fe.hidden_attribute ? flag_hidden : 0)
			| (fe.executable_attribute ? flag_executable : 0)
			| (fe.symlink_attribute ? flag_symlink : 0);
	}

#ifndef TORRENT_NO_DEPRECATE
	void file_storage::set_file_base(int index, boost::int64_t off)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	boost::int64_t file_storage::file_base(int index) const
	{
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	sha1_hash file_storage::hash(internal_file_entry const& fe) const
	{
		int index = &fe - &m_files[0];
		if (index >= int(m_file_hashes.size())) return sha1_hash(0);
		return sha1_hash(m_file_hashes[index]);
	}
	
	std::string const& file_storage::symlink(internal_file_entry const& fe) const
	{
		TORRENT_ASSERT_PRECOND(fe.symlink_index < int(m_symlinks.size()));
		return m_symlinks[fe.symlink_index];
	}

	time_t file_storage::mtime(internal_file_entry const& fe) const
	{
		int index = &fe - &m_files[0];
		if (index >= int(m_mtime.size())) return 0;
		return m_mtime[index];
	}

	int file_storage::file_index(internal_file_entry const& fe) const
	{
		int index = &fe - &m_files[0];
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return index;
	}

	void file_storage::set_file_base(internal_file_entry const& fe, boost::int64_t off)
	{
		int index = &fe - &m_files[0];
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	boost::int64_t file_storage::file_base(internal_file_entry const& fe) const
	{
		int index = &fe - &m_files[0];
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	std::string file_storage::file_path(internal_file_entry const& fe
		, std::string const& save_path) const
	{
		int index = &fe - &m_files[0];
		return file_path(index);
	}

	std::string file_storage::file_name(internal_file_entry const& fe) const
	{
		return fe.filename();
	}

	boost::int64_t file_storage::file_size(internal_file_entry const& fe) const
	{
		return fe.size;
	}

	bool file_storage::pad_file_at(internal_file_entry const& fe) const
	{
		return fe.pad_file;
	}

	boost::int64_t file_storage::file_offset(internal_file_entry const& fe) const
	{
		return fe.offset;
	}

	file_entry file_storage::at(file_storage::iterator i) const
	{ return at(i - m_files.begin()); }
#endif // TORRENT_NO_DEPRECATE

	bool compare_file_entry_size(internal_file_entry const& fe1, internal_file_entry const& fe2)
	{ return fe1.size < fe2.size; }

	void file_storage::reorder_file(int index, int dst)
	{
		TORRENT_ASSERT(index < int(m_files.size()));
		TORRENT_ASSERT(dst < int(m_files.size()));
		TORRENT_ASSERT(dst < index);

		std::iter_swap(m_files.begin() + index, m_files.begin() + dst);
		if (!m_mtime.empty())
		{
			TORRENT_ASSERT(m_mtime.size() == m_files.size());
			if (int(m_mtime.size()) < index) m_mtime.resize(index+1, 0);
			std::iter_swap(m_mtime.begin() + dst, m_mtime.begin() + index);
		}
		if (!m_file_hashes.empty())
		{
			TORRENT_ASSERT(m_file_hashes.size() == m_files.size());
			if (int(m_file_hashes.size()) < index) m_file_hashes.resize(index + 1, NULL);
			std::iter_swap(m_file_hashes.begin() + dst, m_file_hashes.begin() + index);
		}
#ifndef TORRENT_NO_DEPRECATE
		if (!m_file_base.empty())
		{
			TORRENT_ASSERT(m_file_base.size() == m_files.size());
			if (int(m_file_base.size()) < index) m_file_base.resize(index + 1, 0);
			std::iter_swap(m_file_base.begin() + dst, m_file_base.begin() + index);
		}
#endif // TORRENT_DEPRECATED
	}

	void file_storage::optimize(int pad_file_limit, int alignment)
	{
		if (alignment == -1)
			alignment = m_piece_length;

		boost::int64_t off = 0;
		int padding_file = 0;
		for (std::vector<internal_file_entry>::iterator i = m_files.begin();
			i != m_files.end(); ++i)
		{
			if ((off % alignment) == 0)
			{
				// this file position is aligned, pick the largest
				// available file to put here
				std::vector<internal_file_entry>::iterator best_match
					= std::max_element(i, m_files.end()
						, &compare_file_entry_size);

				if (best_match != i)
				{
					int index = best_match - m_files.begin();
					int cur_index = i - m_files.begin();
					reorder_file(index, cur_index);
					i = m_files.begin() + cur_index;
				}
			}
			else if (pad_file_limit >= 0
				&& i->size > boost::uint32_t(pad_file_limit)
				&& i->pad_file == false)
			{
				// if we have pad files enabled, and this file is
				// not piece-aligned and the file size exceeds the
				// limit, and it's not a padding file itself.
				// so add a padding file in front of it
				int pad_size = alignment - (off % alignment);
				
				// find the largest file that fits in pad_size
				std::vector<internal_file_entry>::iterator best_match = m_files.end();

				// if pad_file_limit is 0, it means all files are padded, there's
				// no point in trying to find smaller files to use as filling
				if (pad_file_limit > 0)
				{
					for (std::vector<internal_file_entry>::iterator j = i+1; j < m_files.end(); ++j)
					{
						if (j->size > boost::uint32_t(pad_size)) continue;
						if (best_match == m_files.end() || j->size > best_match->size)
							best_match = j;
					}

					if (best_match != m_files.end())
					{
						// we found one
						// We cannot have found i, because i->size > pad_file_limit
						// which is forced to be no less than alignment. We only
						// look for files <= pad_size, which never is greater than
						// alignment
						TORRENT_ASSERT(best_match != i);
						int index = best_match - m_files.begin();
						int cur_index = i - m_files.begin();
						reorder_file(index, cur_index);
						i = m_files.begin() + cur_index;
						i->offset = off;
						off += i->size;
						continue;
					}
				}

				// we could not find a file that fits in pad_size
				// add a padding file
				// note that i will be set to point to the
				// new pad file. Once we're done adding it, we need
				// to increment i to point to the current file again
				// first add the pad file to the end of the file list
				// then swap it in place. This minimizes the amount
				// of copying of internal_file_entry, which is somewhat
				// expensive (until we have move semantics)
				int cur_index = i - m_files.begin();
				int index = m_files.size();
				m_files.push_back(internal_file_entry());
				++m_num_files;
				internal_file_entry& e = m_files.back();
				// i may have been invalidated, refresh it
				i = m_files.begin() + cur_index;
				e.size = pad_size;
				e.offset = off;
				char name[30];
				snprintf(name, sizeof(name), ".____padding_file/%d", padding_file);
				std::string path = combine_path(m_name, name);
				e.set_name(path.c_str());
				e.pad_file = true;
				off += pad_size;
				++padding_file;

				if (!m_mtime.empty()) m_mtime.resize(index + 1, 0);
				if (!m_file_hashes.empty()) m_file_hashes.resize(index + 1, NULL);
#ifndef TORRENT_NO_DEPRECATE
				if (!m_file_base.empty()) m_file_base.resize(index + 1, 0);
#endif

				reorder_file(index, cur_index);

				TORRENT_ASSERT((off % alignment) == 0);
				continue;
			}
			i->offset = off;
			off += i->size;
		}
		m_total_size = off;
	}

	void file_storage::unload()
	{
		std::vector<internal_file_entry>().swap(m_files);
		std::vector<char const*>().swap(m_file_hashes);
		std::vector<std::string>().swap(m_symlinks);
		std::vector<time_t>().swap(m_mtime);
#ifndef TORRENT_NO_DEPRECATE
		std::vector<boost::int64_t>().swap(m_file_base);
#endif
		std::vector<std::string>().swap(m_paths);
	}
}

