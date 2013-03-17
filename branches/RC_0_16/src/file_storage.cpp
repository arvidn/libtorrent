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

#include "libtorrent/pch.hpp"

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
		TORRENT_ASSERT(index >= 0 && index < num_pieces());
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
		std::string parent = parent_path(e.filename());
		if (parent.empty())
		{
			e.path_index = -1;
		}
		else
		{
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
	}

	file_entry::file_entry(): offset(0), size(0), file_base(0)
		, mtime(0), pad_file(false), hidden_attribute(false)
		, executable_attribute(false)
		, symlink_attribute(false)
	{}

	file_entry::~file_entry() {}

	internal_file_entry::~internal_file_entry() { if (name_len == 0) free((void*)name); }

	internal_file_entry::internal_file_entry(internal_file_entry const& fe)
		: name(0)
		, offset(fe.offset)
		, symlink_index(fe.symlink_index)
		, size(fe.size)
		, name_len(fe.name_len)
		, pad_file(fe.pad_file)
		, hidden_attribute(fe.hidden_attribute)
		, executable_attribute(fe.executable_attribute)
		, symlink_attribute(fe.symlink_attribute)
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
		set_name(fe.filename().c_str());
		return *this;
	}

	void internal_file_entry::set_name(char const* n, int borrow_chars)
	{
		TORRENT_ASSERT(borrow_chars >= 0);
		if (borrow_chars > 1023) borrow_chars = 1023;
		if (name_len == 0) free((void*)name);
		if (n == 0 || *n == 0)
		{
			TORRENT_ASSERT(borrow_chars == 0);
			name = 0;
		}
		else
		{
			name = borrow_chars ? n : allocate_string_copy(n);
		}
		name_len = borrow_chars;
	}

	std::string internal_file_entry::filename() const
	{
		if (name_len) return std::string(name, name_len);
		return name ? name : "";
	}

#if TORRENT_USE_WSTRING
	void file_storage::set_name(std::wstring const& n)
	{
		std::string utf8;
		wchar_utf8(n, utf8);
		m_name = utf8;
	}

	void file_storage::rename_file(int index, std::wstring const& new_filename)
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
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
#endif // TORRENT_USE_WSTRING

	void file_storage::rename_file(int index, std::string const& new_filename)
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		m_files[index].set_name(new_filename.c_str());
		update_path_index(m_files[index]);
	}

	namespace
	{
		bool compare_file_offset(internal_file_entry const& lhs, internal_file_entry const& rhs)
		{
			return lhs.offset < rhs.offset;
		}
	}

	file_storage::iterator file_storage::file_at_offset(size_type offset) const
	{
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = offset;
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<internal_file_entry>::const_iterator file_iter = std::upper_bound(
			begin(), end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != begin());
		--file_iter;
		return file_iter;
	}

	std::vector<file_slice> file_storage::map_block(int piece, size_type offset
		, int size) const
	{
		TORRENT_ASSERT(num_files() > 0);
		std::vector<file_slice> ret;

		if (m_files.empty()) return ret;

		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = piece * (size_type)m_piece_length + offset;
		TORRENT_ASSERT(target.offset + size <= m_total_size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<internal_file_entry>::const_iterator file_iter = std::upper_bound(
			begin(), end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != begin());
		--file_iter;

		size_type file_offset = target.offset - file_iter->offset;
		for (; size > 0; file_offset -= file_iter->size, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != end());
			if (file_offset < file_iter->size)
			{
				file_slice f;
				f.file_index = file_iter - begin();
				f.offset = file_offset + file_base(*file_iter);
				f.size = (std::min)(file_iter->size - file_offset, (size_type)size);
				TORRENT_ASSERT(f.size <= size);
				size -= int(f.size);
				file_offset += f.size;
				ret.push_back(f);
			}
			
			TORRENT_ASSERT(size >= 0);
		}
		return ret;
	}

	file_entry file_storage::at(file_storage::iterator i) const
	{ return at(i - begin()); }

	file_entry file_storage::at(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		file_entry ret;
		internal_file_entry const& ife = m_files[index];
		ret.path = file_path(ife);
		ret.offset = ife.offset;
		ret.size = ife.size;
		ret.file_base = file_base(ife);
		ret.mtime = mtime(ife);
		ret.pad_file = ife.pad_file;
		ret.hidden_attribute = ife.hidden_attribute;
		ret.executable_attribute = ife.executable_attribute;
		ret.symlink_attribute = ife.symlink_attribute;
		if (ife.symlink_index >= 0) ret.symlink_path = symlink(ife);
		ret.filehash = hash(ife);
		return ret;
	}

	peer_request file_storage::map_file(int file_index, size_type file_offset
		, int size) const
	{
		TORRENT_ASSERT(file_index < num_files());
		TORRENT_ASSERT(file_index >= 0);

		peer_request ret;
		if (file_index < 0 || file_index >= num_files())
		{
			ret.piece = m_num_pieces;
			ret.start = 0;
			ret.length = 0;
			return ret;
		}

		size_type offset = file_offset + internal_at(file_index).offset;

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
				ret.length = total_size() - offset;
		}
		return ret;
	}

	void file_storage::add_file(std::string const& file, size_type size, int flags
		, std::time_t mtime, std::string const& symlink_path)
	{
		TORRENT_ASSERT(size >= 0);
		if (size < 0) size = 0;
		if (!has_parent_path(file))
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT(m_files.empty());
			m_name = file;
		}
		else
		{
			if (m_files.empty())
				m_name = split_path(file).c_str();
		}
		TORRENT_ASSERT(m_name == split_path(file).c_str());
		m_files.push_back(internal_file_entry());
		internal_file_entry& e = m_files.back();
		e.set_name(file.c_str());
		e.size = size;
		e.offset = m_total_size;
		e.pad_file = (flags & pad_file) != 0;
		e.hidden_attribute = (flags & attribute_hidden) != 0;
		e.executable_attribute = (flags & attribute_executable) != 0;
		e.symlink_attribute = (flags & attribute_symlink) != 0;
		if (e.symlink_attribute)
		{
			e.symlink_index = m_symlinks.size();
			m_symlinks.push_back(symlink_path);
		}
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
		TORRENT_ASSERT(ent.size >= 0);
		if (!has_parent_path(ent.path))
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT(m_files.empty());
			m_name = ent.path;
		}
		else
		{
			if (m_files.empty())
				m_name = split_path(ent.path).c_str();
		}
		internal_file_entry ife(ent);
		m_files.push_back(ife);
		internal_file_entry& e = m_files.back();
		if (e.size < 0) e.size = 0;
		e.offset = m_total_size;
		m_total_size += e.size;
		if (filehash)
		{
			if (m_file_hashes.size() < m_files.size()) m_file_hashes.resize(m_files.size());
			m_file_hashes[m_files.size() - 1] = filehash;
		}
		if (!ent.symlink_path.empty())
		{
			e.symlink_index = m_symlinks.size();
			m_symlinks.push_back(ent.symlink_path);
		}
		if (ent.mtime)
		{
			if (m_mtime.size() < m_files.size()) m_mtime.resize(m_files.size());
			m_mtime[m_files.size() - 1] = ent.mtime;
		}
		if (ent.file_base) set_file_base(e, ent.file_base);
		update_path_index(e);
	}

	sha1_hash file_storage::hash(int index) const
	{
		if (index >= int(m_file_hashes.size())) return sha1_hash(0);
		return sha1_hash(m_file_hashes[index]);
	}
	
	std::string const& file_storage::symlink(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];
		TORRENT_ASSERT(fe.symlink_index < int(m_symlinks.size()));
		return m_symlinks[fe.symlink_index];
	}

	time_t file_storage::mtime(int index) const
	{
		if (index >= int(m_mtime.size())) return 0;
		return m_mtime[index];
	}

	int file_storage::file_index(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		return index;
	}

	void file_storage::set_file_base(int index, size_type off)
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	size_type file_storage::file_base(int index) const
	{
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	std::string file_storage::file_path(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		internal_file_entry const& fe = m_files[index];
		TORRENT_ASSERT(fe.path_index >= -1 && fe.path_index < int(m_paths.size()));
		if (fe.path_index == -1) return fe.filename();
		return combine_path(m_paths[fe.path_index], fe.filename());
	}

	size_type file_storage::file_size(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		return m_files[index].size;
	}

	sha1_hash file_storage::hash(internal_file_entry const& fe) const
	{
		int index = &fe - &m_files[0];
		if (index >= int(m_file_hashes.size())) return sha1_hash(0);
		return sha1_hash(m_file_hashes[index]);
	}
	
	std::string const& file_storage::symlink(internal_file_entry const& fe) const
	{
		TORRENT_ASSERT(fe.symlink_index < int(m_symlinks.size()));
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
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		return index;
	}

	void file_storage::set_file_base(internal_file_entry const& fe, size_type off)
	{
		int index = &fe - &m_files[0];
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	size_type file_storage::file_base(internal_file_entry const& fe) const
	{
		int index = &fe - &m_files[0];
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	std::string file_storage::file_path(internal_file_entry const& fe) const
	{
		TORRENT_ASSERT(fe.path_index >= -1 && fe.path_index < int(m_paths.size()));
		if (fe.path_index == -1) return fe.filename();
		return combine_path(m_paths[fe.path_index], fe.filename());
	}

	size_type file_storage::file_size(internal_file_entry const& fe) const
	{
		return fe.size;
	}

	bool compare_file_entry_size(internal_file_entry const& fe1, internal_file_entry const& fe2)
	{ return fe1.size < fe2.size; }

	void file_storage::reorder_file(int index, int dst)
	{
		TORRENT_ASSERT(index < int(m_files.size()));
		TORRENT_ASSERT(dst < int(m_files.size()));
		TORRENT_ASSERT(dst < index);

		internal_file_entry e = m_files[index];
		m_files.erase(m_files.begin() + index);
		m_files.insert(m_files.begin() + dst, e);
		if (!m_mtime.empty())
		{
			TORRENT_ASSERT(m_mtime.size() == m_files.size());
			time_t mtime = 0;
			if (int(m_mtime.size()) > index)
			{
				mtime = m_mtime[index];
				m_mtime.erase(m_mtime.begin() + index);
			}
			if (dst > int(m_mtime.size())) m_mtime.resize(dst, 0);
			m_mtime.insert(m_mtime.begin() + dst, mtime);
		}
		if (!m_file_hashes.empty())
		{
			TORRENT_ASSERT(m_file_hashes.size() == m_files.size());
			char const* fh = 0;
			if (int(m_file_hashes.size()) > index)
			{
				fh = m_file_hashes[index];
				m_file_hashes.erase(m_file_hashes.begin() + index);
			}
			if (int(m_file_hashes.size()) < dst) m_file_hashes.resize(dst, NULL);
			m_file_hashes.insert(m_file_hashes.begin() + dst, fh);
		}
		if (!m_file_base.empty())
		{
			TORRENT_ASSERT(m_file_base.size() == m_files.size());
			size_type base = 0;
			if (int(m_file_base.size()) > index)
			{
				base = m_file_base[index];
				m_file_base.erase(m_file_base.begin() + index);
			}
			m_file_base.insert(m_file_base.begin() + dst, base);
		}
	}

	void file_storage::optimize(int pad_file_limit)
	{
		// the main purpuse of padding is to optimize disk
		// I/O. This is a conservative memory page size assumption
		int alignment = 16*1024;

		// it doesn't make any sense to pad files that
		// are smaller than the alignment
		if (pad_file_limit >= 0 && pad_file_limit < alignment)
			pad_file_limit = alignment;

		size_type off = 0;
		int padding_file = 0;
		for (std::vector<internal_file_entry>::iterator i = m_files.begin();
			i != m_files.end(); ++i)
		{
			if ((off & (alignment-1)) == 0)
			{
				// this file position is aligned, pick the largest
				// available file to put here
				std::vector<internal_file_entry>::iterator best_match
					= std::max_element(i, m_files.end()
						, &compare_file_entry_size);

				if (best_match != i)
				{
					int index = file_index(*best_match);
					int cur_index = file_index(*i);
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
				int pad_size = alignment - (off & (alignment-1));
				
				// find the largest file that fits in pad_size
				std::vector<internal_file_entry>::iterator best_match = m_files.end();
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
					int index = file_index(*best_match);
					int cur_index = file_index(*i);
					reorder_file(index, cur_index);
					i = m_files.begin() + cur_index;
					i->offset = off;
					off += i->size;
					continue;
				}

				// we could not find a file that fits in pad_size
				// add a padding file
				// note that i will be set to point to the
				// new pad file. Once we're done adding it, we need
				// to increment i to point to the current file again
				internal_file_entry e;
				int cur_index = file_index(*i);
				i = m_files.insert(i, e);
				i->size = pad_size;
				i->offset = off;
				char name[30];
				snprintf(name, sizeof(name), ".____padding_file/%d", padding_file);
				std::string path = combine_path(m_name, name);
				i->set_name(path.c_str());
				i->pad_file = true;
				off += pad_size;
				++padding_file;

				if (int(m_mtime.size()) > cur_index)
					m_mtime.insert(m_mtime.begin() + cur_index, 0);

				if (int(m_file_hashes.size()) > cur_index)
					m_file_hashes.insert(m_file_hashes.begin()
						+ cur_index, (char const*)NULL);

				if (int(m_file_base.size()) > cur_index)
					m_file_base.insert(m_file_base.begin() + cur_index, 0);

				// skip the pad file we just added and point
				// at the current file again
				++i;
			}
			i->offset = off;
			off += i->size;
		}
		m_total_size = off;
	}
}

