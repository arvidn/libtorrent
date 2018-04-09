/*

Copyright (c) 2012-2018, Arvid Norberg
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


/*

  The part_file file format is an array of piece sized blocks with
  a simple header. For a given number of pieces, the header has a
  fixed size. The header size is rounded up to an even multiple of
  1024, in an attempt at improving disk I/O performance by aligning
  reads and writes to clusters on the drive. This is the file header
  format. All values are stored big endian on disk.


  // the size of the torrent (and can be used to calculate the size
  // of the file header)
  uint32_t num_pieces;
  
  // the number of bytes in each piece. This determines the size of
  // each slot in the part file. This is typically an even power of 2,
  // but it is not guaranteed to be.
  uint32_t piece_size;

  // this is an array specifying which slots a particular piece resides in,
  // A value of 0xffffffff (-1 if you will) means the piece is not in the part_file
  // Any other value means the piece resides in the slot with that index
  uint32_t piece[num_pieces];

  // unused, n is defined as the number to align the size of this
  // header to an even multiple of 1024 bytes.
  uint8_t padding[n];
 
*/

#include "libtorrent/part_file.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/assert.hpp"
#include <boost/scoped_array.hpp>

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

namespace
{
	// round up to even kilobyte
	int round_up(int n)
	{ return (n + 1023) & ~0x3ff; }
}

namespace libtorrent
{
	part_file::part_file(std::string const& path, std::string const& name
		, int num_pieces, int piece_size)
		: m_path(path)
		, m_name(name)
		, m_num_allocated(0)
		, m_max_pieces(num_pieces)
		, m_piece_size(piece_size)
		, m_header_size(round_up((2 + num_pieces) * 4))
		, m_dirty_metadata(false)
	{
		TORRENT_ASSERT(num_pieces > 0);
		TORRENT_ASSERT(m_piece_size > 0);

		error_code ec;
		std::string fn = combine_path(m_path, m_name);
		m_file.open(fn, file::read_only, ec);
		if (!ec)
		{
			// parse header
			boost::scoped_array<boost::uint32_t> header(new boost::uint32_t[m_header_size / 4]);
			file::iovec_t b = {header.get(), size_t(m_header_size) };
			int n = m_file.readv(0, &b, 1, ec);
			if (ec) return;

			// we don't have a full header. consider the file empty
			if (n < m_header_size) return;
			using namespace libtorrent::detail;

			char* ptr = reinterpret_cast<char*>(header.get());
			// we have a header. Parse it
			int num_pieces_ = read_uint32(ptr);
			int piece_size_ = read_uint32(ptr);

			// if there is a mismatch in number of pieces or piece size
			// consider the file empty and overwrite anything in there
			if (num_pieces != num_pieces_ || m_piece_size != piece_size_) return;

			// this is used to determine which slots are free, and how many
			// slots are allocated
			std::vector<bool> free_slots;
			free_slots.resize(num_pieces, true);

			for (int i = 0; i < num_pieces; ++i)
			{
				int slot = read_uint32(ptr);
				if (slot == 0xffffffff) continue;

				// invalid part-file
				TORRENT_ASSERT(slot < num_pieces);
				if (slot >= num_pieces) continue;

				if (slot >= m_num_allocated)
					m_num_allocated = slot + 1;

				free_slots[slot] = false;
				m_piece_map[i] = slot;
			}

			// now, populate the free_list with the "holes"
			for (int i = 0; i < m_num_allocated; ++i)
			{
				if (free_slots[i]) m_free_slots.push_back(i);
			}

			m_file.close();
		}
	}

	part_file::~part_file()
	{
		error_code ec;
		flush_metadata_impl(ec);
	}

	int part_file::allocate_slot(int piece)
	{
		// the mutex is assumed to be held here, since this is a private function

		TORRENT_ASSERT(m_piece_map.find(piece) == m_piece_map.end());
		int slot = -1;
		if (!m_free_slots.empty())
		{
			slot = m_free_slots.front();
			m_free_slots.erase(m_free_slots.begin());
		}
		else
		{
			slot = m_num_allocated;
			++m_num_allocated;
		}

		m_piece_map[piece] = slot;
		m_dirty_metadata = true;
		return slot;
	}

	int part_file::writev(file::iovec_t const* bufs, int num_bufs, int piece, int offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		mutex::scoped_lock l(m_mutex);

		open_file(file::read_write, ec);
		if (ec) return -1;

		int slot = -1;
		boost::unordered_map<int, int>::iterator i = m_piece_map.find(piece);
		if (i == m_piece_map.end())
			slot = allocate_slot(piece);
		else
			slot = i->second;

		l.unlock();

		boost::int64_t slot_offset = boost::int64_t(m_header_size) + boost::int64_t(slot) * m_piece_size;
		return m_file.writev(slot_offset + offset, bufs, num_bufs, ec);
	}

	int part_file::readv(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		mutex::scoped_lock l(m_mutex);

		boost::unordered_map<int, int>::iterator i = m_piece_map.find(piece);
		if (i == m_piece_map.end())
		{
			ec = error_code(boost::system::errc::no_such_file_or_directory
				, boost::system::generic_category());
			return -1;
		}

		int slot = i->second;

		open_file(file::read_write, ec);
		if (ec) return -1;

		l.unlock();

		boost::int64_t slot_offset = boost::int64_t(m_header_size) + boost::int64_t(slot) * m_piece_size;
		return m_file.readv(slot_offset + offset, bufs, num_bufs, ec);
	}

	void part_file::open_file(int mode, error_code& ec)
	{
		if (m_file.is_open()
			&& ((m_file.open_mode() & file::rw_mask) == mode
				|| mode == file::read_only)) return;

		std::string fn = combine_path(m_path, m_name);
		m_file.open(fn, mode, ec);
		if (((mode & file::rw_mask) != file::read_only)
			&& ec == boost::system::errc::no_such_file_or_directory)
		{
			// this means the directory the file is in doesn't exist.
			// so create it
			ec.clear();
			create_directories(m_path, ec);

			if (ec) return;
			m_file.open(fn, mode, ec);
		}
	}

	void part_file::free_piece(int piece)
	{
		mutex::scoped_lock l(m_mutex);

		boost::unordered_map<int, int>::iterator i = m_piece_map.find(piece);
		if (i == m_piece_map.end()) return;

		// TODO: what do we do if someone is currently reading from the disk
		// from this piece? does it matter? Since we won't actively erase the
		// data from disk, but it may be overwritten soon, it's probably not that
		// big of a deal

		m_free_slots.push_back(i->second);
		m_piece_map.erase(i);
		m_dirty_metadata = true;
	}

	void part_file::move_partfile(std::string const& path, error_code& ec)
	{
		mutex::scoped_lock l(m_mutex);

		flush_metadata_impl(ec);
		if (ec) return;

		m_file.close();

		if (!m_piece_map.empty())
		{
			std::string old_path = combine_path(m_path, m_name);
			std::string new_path = combine_path(path, m_name);

			rename(old_path, new_path, ec);
			if (ec == boost::system::errc::no_such_file_or_directory)
				ec.clear();

			if (ec)
			{
				copy_file(old_path, new_path, ec);
				if (ec) return;
				remove(old_path, ec);
			}
		}
		m_path = path;
	}

	void part_file::import_file(file& f, boost::int64_t offset
		, boost::int64_t size, error_code& ec)
	{
		TORRENT_UNUSED(f);
		TORRENT_UNUSED(offset);
		TORRENT_UNUSED(size);
		TORRENT_UNUSED(ec);

		// not implemented
		ec.assign(boost::system::errc::operation_not_supported
			, boost::system::generic_category());
	}

	void part_file::export_file(file& f, boost::int64_t offset, boost::int64_t size, error_code& ec)
	{
		mutex::scoped_lock l(m_mutex);

		int piece = offset / m_piece_size;
		int const end = ((offset + size) + m_piece_size - 1) / m_piece_size;

		boost::scoped_array<char> buf;

		boost::int64_t piece_offset = offset - boost::int64_t(piece) * m_piece_size;
		boost::int64_t file_offset = 0;
		for (; piece < end; ++piece)
		{
			boost::unordered_map<int, int>::iterator i = m_piece_map.find(piece);
			int const block_to_copy = (std::min)(m_piece_size - piece_offset, size);
			if (i != m_piece_map.end())
			{
				int const slot = i->second;
				open_file(file::read_only, ec);
				if (ec) return;

				if (!buf) buf.reset(new char[m_piece_size]);

				boost::int64_t const slot_offset = boost::int64_t(m_header_size)
					+ boost::int64_t(slot) * m_piece_size;

				// don't hold the lock during disk I/O
				l.unlock();

				file::iovec_t v = { buf.get(), size_t(block_to_copy) };
				v.iov_len = m_file.readv(slot_offset + piece_offset, &v, 1, ec);
				TORRENT_ASSERT(!ec);
				if (ec || v.iov_len == 0) return;

				boost::int64_t ret = f.writev(file_offset, &v, 1, ec);
				TORRENT_ASSERT(ec || ret == v.iov_len);
				if (ec || ret != v.iov_len) return;

				// we're done with the disk I/O, grab the lock again to update
				// the slot map
				l.lock();

				if (block_to_copy == m_piece_size)
				{
					// since we released the lock, it's technically possible that
					// another thread removed this slot map entry, and invalidated
					// our iterator. Now that we hold the lock again, perform
					// another lookup to be sure.
					boost::unordered_map<int, int>::iterator j = m_piece_map.find(piece);
					if (j != m_piece_map.end())
					{
						// if the slot moved, that's really suspicious
						TORRENT_ASSERT(j->second == slot);
						m_free_slots.push_back(j->second);
						m_piece_map.erase(j);
						m_dirty_metadata = true;
					}
				}
			}
			file_offset += block_to_copy;
			piece_offset = 0;
			size -= block_to_copy;
		}
	}

	void part_file::flush_metadata(error_code& ec)
	{
		mutex::scoped_lock l(m_mutex);

		flush_metadata_impl(ec);
	}

	// TODO: instead of rebuilding the whole file header
	// and flushing it, update the slot entries as we go
	void part_file::flush_metadata_impl(error_code& ec)
	{
		// do we need to flush the metadata?
		if (m_dirty_metadata == false) return;

		if (m_piece_map.empty())
		{
			m_file.close();

			// if we don't have any pieces left in the
			// part file, remove it
			std::string p = combine_path(m_path, m_name);
			remove(p, ec);

			if (ec == boost::system::errc::no_such_file_or_directory)
				ec.clear();
			return;
		}

		open_file(file::read_write, ec);
		if (ec) return;

		boost::scoped_array<boost::uint32_t> header(new boost::uint32_t[m_header_size / 4]);

		using namespace libtorrent::detail;

		char* ptr = reinterpret_cast<char*>(header.get());

		write_uint32(m_max_pieces, ptr);
		write_uint32(m_piece_size, ptr);

		for (int piece = 0; piece < m_max_pieces; ++piece)
		{
			boost::unordered_map<int, int>::iterator i = m_piece_map.find(piece);
			int slot = 0xffffffff;
			if (i != m_piece_map.end())
				slot = i->second;
			write_uint32(slot, ptr);
		}
		memset(ptr, 0, m_header_size - (ptr - reinterpret_cast<char*>(header.get())));

#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_MEM_IS_DEFINED(header.get(), m_header_size);
#endif
		file::iovec_t b = {header.get(), size_t(m_header_size) };
		m_file.writev(0, &b, 1, ec);
		if (ec) return;
	}
}

