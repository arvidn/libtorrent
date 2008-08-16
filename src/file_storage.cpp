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

	void file_storage::rename_file(int index, std::string const& new_filename)
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_files.size()));
		m_files[index].path = new_filename;
	}

	file_storage::iterator file_storage::file_at_offset(size_type offset) const
	{
		// TODO: do a binary search
		std::vector<file_entry>::const_iterator i;
		for (i = begin(); i != end(); ++i)
		{
			if (i->offset <= offset && i->offset + i->size > offset)
				return i;
		}
		return i;
	}

	std::vector<file_slice> file_storage::map_block(int piece, size_type offset
		, int size_) const
	{
		TORRENT_ASSERT(num_files() > 0);
		std::vector<file_slice> ret;

		size_type start = piece * (size_type)m_piece_length + offset;
		size_type size = size_;
		TORRENT_ASSERT(start + size <= m_total_size);

		// find the file iterator and file offset
		// TODO: do a binary search on the file offsets
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		int counter = 0;
		for (file_iter = begin();; ++counter, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != end());
			if (file_offset < file_iter->size)
			{
				file_slice f;
				f.file_index = counter;
				f.offset = file_offset + file_iter->file_base;
				f.size = (std::min)(file_iter->size - file_offset, (size_type)size);
				size -= f.size;
				file_offset += f.size;
				ret.push_back(f);
			}
			
			TORRENT_ASSERT(size >= 0);
			if (size <= 0) break;

			file_offset -= file_iter->size;
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
		ret.start = int(offset - ret.piece * piece_length());
		ret.length = size;
		return ret;
	}

	void file_storage::add_file(fs::path const& file, size_type size)
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
		file_entry e;
		m_files.push_back(e);
		m_files.back().size = size;
		m_files.back().path = file;
		m_files.back().offset = m_total_size;
		m_total_size += size;
	}

	void file_storage::add_file(file_entry const& e)
	{
		add_file(e.path, e.size);
	}
}

