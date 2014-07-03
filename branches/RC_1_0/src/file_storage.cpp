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
#include <cstdio>
#include <algorithm>

namespace libtorrent
{
	file_storage::file_storage()
		: m_total_size(0)
		, m_num_pieces(0)
		, m_piece_length(0)
	{}

	void file_storage::reserve(int num_files)
	{
		m_files.reserve(num_files);
	}

	int file_storage::piece_size(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			size_type size_except_last = num_pieces() - 1;
			size_except_last *= size_type(piece_length());
			size_type size = total_size() - size_except_last;
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

	void file_storage::update_path_index(internal_file_entry& e)
	{
		std::string fname = e.filename();
		if (is_complete(fname))
		{
			e.path_index = -2;
			return;
		}
		std::string parent = parent_path(fname);

		if (parent.empty())
		{
			e.path_index = -1;
			return;
		}

		if (parent.size() >= m_name.size()
			&& parent.compare(0, m_name.size(), m_name) == 0
			&& (parent.size() == m_name.size()
#ifdef TORRENT_WINDOWS
				|| parent[m_name.size()] == '\\'
#endif
				|| parent[m_name.size()] == '/'
			))
		{
			parent.erase(parent.begin(), parent.begin() + m_name.size()
				+ (m_name.size() == parent.size()?0:1));
			e.no_root_dir = false;
		}
		else
		{
			e.no_root_dir = true;
		}

		// do we already have this path in the path list?
		std::vector<std::string>::reverse_iterator p
			= std::find(m_paths.rbegin(), m_paths.rend(), parent);

		if (p == m_paths.rend())
		{
			// no, we don't. add it
			e.path_index = m_paths.size();
			m_paths.push_back(parent);
		}
		else
		{
			// yes we do. use it
			e.path_index = p.base() - m_paths.begin() - 1;
		}
		e.set_name(filename(e.filename()).c_str());
	}

	file_entry::file_entry(): offset(0), size(0), file_base(0)
		, mtime(0), pad_file(false), hidden_attribute(false)
		, executable_attribute(false)
		, symlink_attribute(false)
	{}

	file_entry::~file_entry() {}

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
		set_name(fe.filename().c_str());
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

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE
	void file_storage::set_name(std::wstring const& n)
	{
		std::string utf8;
		wchar_utf8(n, utf8);
		m_name = utf8;
	}

	void file_storage::rename_file(int index, std::wstring const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		std::string utf8;
		wchar_utf8(new_filename, utf8);
		m_files[index].set_name(utf8.c_str());
		update_path_index(m_files[index]);
	}

	void file_storage::add_file(std::wstring const& file, size_type size, int flags
		, std::time_t mtime, std::string const& symlink_path)
	{
		std::string utf8;
		wchar_utf8(file, utf8);
		add_file(utf8, size, flags, mtime, symlink_path);
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

	void file_storage::rename_file(int index, std::string const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		m_files[index].set_name(new_filename.c_str());
		update_path_index(m_files[index]);
	}

	void file_storage::rename_file_borrow(int index, char const* new_filename, int len)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		m_files[index].set_name(new_filename, true, len);
	}

	namespace
	{
		bool compare_file_offset(internal_file_entry const& lhs, internal_file_entry const& rhs)
		{
			return lhs.offset < rhs.offset;
		}
	}

#ifndef TORRENT_NO_DEPRECATE
	file_storage::iterator file_storage::file_at_offset_deprecated(size_type offset) const
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

	file_storage::iterator file_storage::file_at_offset(size_type offset) const
	{
		return file_at_offset_deprecated(offset);
	}
#endif

	int file_storage::file_index_at_offset(size_type offset) const
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

	std::vector<file_slice> file_storage::map_block(int piece, size_type offset
		, int size) const
	{
		TORRENT_ASSERT_PRECOND(num_files() > 0);
		std::vector<file_slice> ret;

		if (m_files.empty()) return ret;

		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = piece * (size_type)m_piece_length + offset;
		TORRENT_ASSERT_PRECOND(target.offset + size <= m_total_size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<internal_file_entry>::const_iterator file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;

		size_type file_offset = target.offset - file_iter->offset;
		for (; size > 0; file_offset -= file_iter->size, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != m_files.end());
			if (file_offset < size_type(file_iter->size))
			{
				file_slice f;
				f.file_index = file_iter - m_files.begin();
				f.offset = file_offset + file_base(f.file_index);
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

	peer_request file_storage::map_file(int file_index, size_type file_offset
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

		size_type offset = file_offset + this->file_offset(file_index);

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

	void file_storage::add_file(std::string const& file, size_type size, int flags
		, std::time_t mtime, std::string const& symlink_path)
	{
		TORRENT_ASSERT_PRECOND(!is_complete(file));
		TORRENT_ASSERT_PRECOND(size >= 0);
		if (size < 0) size = 0;
		if (!has_parent_path(file))
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT_PRECOND(m_files.empty());
			m_name = file;
		}
		else
		{
			if (m_files.empty())
				m_name = split_path(file).c_str();
		}
		TORRENT_ASSERT_PRECOND(m_name == split_path(file).c_str());
		m_files.push_back(internal_file_entry());
		internal_file_entry& e = m_files.back();
		e.set_name(file.c_str());
		e.size = size;
		e.offset = m_total_size;
		e.pad_file = (flags & pad_file) != 0;
		e.hidden_attribute = (flags & attribute_hidden) != 0;
		e.executable_attribute = (flags & attribute_executable) != 0;
		if ((flags & attribute_symlink) && m_symlinks.size() < internal_file_entry::not_a_symlink - 1)
		{
			e.symlink_attribute = 1;
			e.symlink_index = m_symlinks.size();
			m_symlinks.push_back(symlink_path);
		}
		else
			e.symlink_attribute = 0;

		if (mtime)
		{
			if (m_mtime.size() < m_files.size()) m_mtime.resize(m_files.size());
			m_mtime[m_files.size() - 1] = mtime;
		}
		
		update_path_index(e);
		m_total_size += size;
	}

	void file_storage::add_file(file_entry const& ent, char const* filehash)
	{
		TORRENT_ASSERT_PRECOND(ent.size >= 0);
		if (!has_parent_path(ent.path))
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT_PRECOND(m_files.empty());
			m_name = ent.path;
		}
		else
		{
			if (m_files.empty())
				m_name = split_path(ent.path).c_str();
		}
		internal_file_entry ife(ent);
		int file_index = m_files.size();
		m_files.push_back(ife);
		internal_file_entry& e = m_files.back();
		e.offset = m_total_size;
		m_total_size += e.size;
		if (filehash)
		{
			if (m_file_hashes.size() < m_files.size()) m_file_hashes.resize(m_files.size());
			m_file_hashes[m_files.size() - 1] = filehash;
		}
		if (!ent.symlink_path.empty() && m_symlinks.size() < internal_file_entry::not_a_symlink - 1)
		{
			e.symlink_index = m_symlinks.size();
			m_symlinks.push_back(ent.symlink_path);
		}
		if (ent.mtime)
		{
			if (m_mtime.size() < m_files.size()) m_mtime.resize(m_files.size());
			m_mtime[m_files.size() - 1] = ent.mtime;
		}
		if (ent.file_base) set_file_base(file_index, ent.file_base);
		update_path_index(e);
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

	void file_storage::set_file_base(int index, size_type off)
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	size_type file_storage::file_base(int index) const
	{
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	std::string file_storage::file_path(int index, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];

		// -2 means this is an absolute path filename
		if (fe.path_index == -2) return fe.filename();

		// -1 means no path
		if (fe.path_index == -1) return combine_path(save_path, fe.filename());

		if (fe.no_root_dir)
			return combine_path(save_path
				, combine_path(m_paths[fe.path_index]
				, fe.filename()));

		return combine_path(save_path
			, combine_path(m_name
			, combine_path(m_paths[fe.path_index]
			, fe.filename())));
	}

	std::string file_storage::file_name(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];
		return fe.filename();
	}

	size_type file_storage::file_size(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return m_files[index].size;
	}

	bool file_storage::pad_file_at(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return m_files[index].pad_file;
	}

	size_type file_storage::file_offset(int index) const
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

	void file_storage::set_file_base(internal_file_entry const& fe, size_type off)
	{
		int index = &fe - &m_files[0];
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	size_type file_storage::file_base(internal_file_entry const& fe) const
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

	size_type file_storage::file_size(internal_file_entry const& fe) const
	{
		return fe.size;
	}

	bool file_storage::pad_file_at(internal_file_entry const& fe) const
	{
		return fe.pad_file;
	}

	size_type file_storage::file_offset(internal_file_entry const& fe) const
	{
		return fe.offset;
	}

	file_entry file_storage::at(file_storage::iterator i) const
	{ return at(i - begin()); }
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
		if (!m_file_base.empty())
		{
			TORRENT_ASSERT(m_file_base.size() == m_files.size());
			if (int(m_file_base.size()) < index) m_file_base.resize(index + 1, 0);
			std::iter_swap(m_file_base.begin() + dst, m_file_base.begin() + index);
		}
	}

	void file_storage::optimize(int pad_file_limit, int alignment)
	{
		if (alignment == -1)
			alignment = m_piece_length;

		size_type off = 0;
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
				&& i->size > pad_file_limit
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
						if (j->size > pad_size) continue;
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
				if (!m_file_base.empty()) m_file_base.resize(index + 1, 0);

				reorder_file(index, cur_index);

				TORRENT_ASSERT((off % alignment) == 0);
				continue;
			}
			i->offset = off;
			off += i->size;
		}
		m_total_size = off;
	}
}

