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
#include "libtorrent/utf8.hpp"
#include <boost/bind.hpp>
#include <cstdio>

namespace libtorrent
{
	file_storage::file_storage()
		: m_piece_length(0)
		, m_total_size(0)
		, m_num_pieces(0)
	{}

	int file_storage::piece_size(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			int size = int(total_size()
				- size_type(num_pieces() - 1) * piece_length());
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
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
		m_files[index].path = utf8;
	}

	void file_storage::add_file(fs::wpath const& file, size_type size, int flags
		, std::time_t mtime, fs::path const& symlink_path)
	{
		std::string utf8;
		wchar_utf8(file.string(), utf8);
		add_file(utf8, size, flags, mtime, symlink_path);
	}
#endif

	void file_storage::rename_file(int index, std::string const& new_filename)
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		m_files[index].path = new_filename;
	}

	namespace
	{
		bool compare_file_offset(file_entry const& lhs, file_entry const& rhs)
		{
			return lhs.offset < rhs.offset;
		}
	}

	file_storage::iterator file_storage::file_at_offset(size_type offset) const
	{
		// find the file iterator and file offset
		file_entry target;
		target.offset = offset;
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<file_entry>::const_iterator file_iter = std::upper_bound(
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
		file_entry target;
		target.offset = piece * (size_type)m_piece_length + offset;
		TORRENT_ASSERT(target.offset + size <= m_total_size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		std::vector<file_entry>::const_iterator file_iter = std::upper_bound(
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
				f.offset = file_offset + file_iter->file_base;
				f.size = (std::min)(file_iter->size - file_offset, (size_type)size);
				size -= f.size;
				file_offset += f.size;
				ret.push_back(f);
			}
			
			TORRENT_ASSERT(size >= 0);
		}
		return ret;
	}
	
	peer_request file_storage::map_file(int file_index, size_type file_offset
		, int size) const
	{
		TORRENT_ASSERT(file_index < num_files());
		TORRENT_ASSERT(file_index >= 0);
		size_type offset = file_offset + at(file_index).offset;

		peer_request ret;
		ret.piece = int(offset / piece_length());
		ret.start = int(offset % piece_length());
		ret.length = size;
		return ret;
	}

	void file_storage::add_file(fs::path const& file, size_type size, int flags
		, std::time_t mtime, fs::path const& symlink_path)
	{
		TORRENT_ASSERT(size >= 0);
#if BOOST_VERSION < 103600
		if (!file.has_branch_path())
#else
		if (!file.has_parent_path())
#endif
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT(m_files.empty());
			m_name = file.string();
		}
		else
		{
			if (m_files.empty())
				m_name = *file.begin();
		}
		TORRENT_ASSERT(m_name == *file.begin());
		m_files.push_back(file_entry());
		file_entry& e = m_files.back();
		e.size = size;
		e.path = file;
		e.offset = m_total_size;
		e.pad_file = bool(flags & pad_file);
		e.hidden_attribute = bool(flags & attribute_hidden);
		e.executable_attribute = bool(flags & attribute_executable);
		e.symlink_attribute = bool(flags & attribute_symlink);
		if (e.symlink_attribute) e.symlink_path = symlink_path.string();
		e.mtime = mtime;
		m_total_size += size;
	}

	void file_storage::add_file(file_entry const& ent)
	{
#if BOOST_VERSION < 103600
		if (!ent.path.has_branch_path())
#else
		if (!ent.path.has_parent_path())
#endif
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT(m_files.empty());
			m_name = ent.path.string();
		}
		else
		{
			if (m_files.empty())
				m_name = *ent.path.begin();
		}
		m_files.push_back(ent);
		file_entry& e = m_files.back();
		e.offset = m_total_size;
		m_total_size += ent.size;
	}

	void file_storage::optimize(int pad_file_limit)
	{
		// the main purpuse of padding is to optimize disk
		// I/O. This is a conservative memory page size assumption
		int alignment = 8*1024;

		// it doesn't make any sense to pad files that
		// are smaller than one piece
		if (pad_file_limit >= 0 && pad_file_limit < alignment)
			pad_file_limit = alignment;

		// put the largest file at the front, to make sure
		// it's aligned
		std::vector<file_entry>::iterator i = std::max_element(m_files.begin(), m_files.end()
			, boost::bind(&file_entry::size, _1) < boost::bind(&file_entry::size, _2));

		using std::iter_swap;
		iter_swap(i, m_files.begin());

		size_type off = 0;
		int padding_file = 0;
		for (std::vector<file_entry>::iterator i = m_files.begin();
			i != m_files.end(); ++i)
		{
			if (pad_file_limit >= 0
				&& (off & (alignment-1)) != 0
				&& i->size > pad_file_limit
				&& i->pad_file == false)
			{
				// if we have pad files enabled, and this file is
				// not piece-aligned and the file size exceeds the
				// limit, and it's not a padding file itself.
				// so add a padding file in front of it
				int pad_size = alignment - (off & (alignment-1));
				
				// find the largest file that fits in pad_size
				std::vector<file_entry>::iterator best_match = m_files.end();
				for (std::vector<file_entry>::iterator j = i+1; j < m_files.end(); ++j)
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
					file_entry e = *best_match;
					m_files.erase(best_match);
					i = m_files.insert(i, e);
					i->offset = off;
					off += i->size;
					continue;
				}

				// we could not find a file that fits in pad_size
				// add a padding file
				// note that i will be set to point to the
				// new pad file. Once we're done adding it, we need
				// to increment i to point to the current file again
				file_entry e;
				i = m_files.insert(i, e);
				i->size = pad_size;
				i->offset = off;
				i->file_base = 0;
				char name[10];
				std::sprintf(name, "%d", padding_file);
				i->path = *(i+1)->path.begin();
				i->path /= "_____padding_file_";
				i->path /= name;
				i->pad_file = true;
				off += pad_size;
				++padding_file;
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

