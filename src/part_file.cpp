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
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/path.hpp"

#include <functional> // for std::function
#include <cstdint>

namespace {

	// round up to even kilobyte
	int round_up(int n)
	{ return (n + 1023) & ~0x3ff; }
}

namespace libtorrent {

	part_file::part_file(std::string const& path, std::string const& name
		, int const num_pieces, int const piece_size)
		: m_path(path)
		, m_name(name)
		, m_max_pieces(num_pieces)
		, m_piece_size(piece_size)
		, m_header_size(round_up((2 + num_pieces) * 4))
	{
		TORRENT_ASSERT(num_pieces > 0);
		TORRENT_ASSERT(m_piece_size > 0);

		error_code ec;
		std::string fn = combine_path(m_path, m_name);
		auto f = std::make_shared<file>(fn, open_mode::read_only, ec);
		if (ec) return;

		// parse header
		std::vector<char> header(static_cast<std::size_t>(m_header_size));
		iovec_t b = header;
		int n = int(f->readv(0, b, ec));
		if (ec) return;

		// we don't have a full header. consider the file empty
		if (n < m_header_size) return;
		using namespace libtorrent::detail;

		char* ptr = header.data();
		// we have a header. Parse it
		int const num_pieces_ = int(read_uint32(ptr));
		int const piece_size_ = int(read_uint32(ptr));

		// if there is a mismatch in number of pieces or piece size
		// consider the file empty and overwrite anything in there
		if (num_pieces != num_pieces_ || m_piece_size != piece_size_) return;

		// this is used to determine which slots are free, and how many
		// slots are allocated
		aux::vector<bool, slot_index_t> free_slots;
		free_slots.resize(num_pieces, true);

		for (piece_index_t i = piece_index_t(0); i < piece_index_t(num_pieces); ++i)
		{
			slot_index_t const slot(read_int32(ptr));
			if (static_cast<int>(slot) < 0) continue;

			// invalid part-file
			TORRENT_ASSERT(slot < slot_index_t(num_pieces));
			if (slot >= slot_index_t(num_pieces)) continue;

			if (slot >= m_num_allocated)
				m_num_allocated = next(slot);

			free_slots[slot] = false;
			m_piece_map[i] = slot;
		}

		// now, populate the free_list with the "holes"
		for (slot_index_t i(0); i < m_num_allocated; ++i)
		{
			if (free_slots[i]) m_free_slots.push_back(i);
		}
		m_file = std::move(f);
	}

	part_file::~part_file()
	{
		error_code ec;
		flush_metadata_impl(ec);
	}

	slot_index_t part_file::allocate_slot(piece_index_t const piece)
	{
		// the mutex is assumed to be held here, since this is a private function

		TORRENT_ASSERT(m_piece_map.find(piece) == m_piece_map.end());
		slot_index_t slot(-1);
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

	int part_file::writev(span<iovec_t const> bufs, piece_index_t const piece
		, int const offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		std::unique_lock<std::mutex> l(m_mutex);

		open_file(open_mode::read_write | open_mode::attribute_hidden, ec);
		if (ec) return -1;

		auto const i = m_piece_map.find(piece);
		slot_index_t const slot = (i == m_piece_map.end())
			? allocate_slot(piece) : i->second;

		auto const f = m_file;
		l.unlock();

		return int(f->writev(slot_offset(slot) + offset, bufs, ec));
	}

	int part_file::readv(span<iovec_t const> bufs
		, piece_index_t const piece, int offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		std::unique_lock<std::mutex> l(m_mutex);

		auto const i = m_piece_map.find(piece);
		if (i == m_piece_map.end())
		{
			ec = make_error_code(boost::system::errc::no_such_file_or_directory);
			return -1;
		}

		slot_index_t const slot = i->second;
		open_file(open_mode::read_only | open_mode::attribute_hidden, ec);
		if (ec) return -1;

		auto const f = m_file;
		l.unlock();

		return int(f->readv(slot_offset(slot) + offset, bufs, ec));
	}

	void part_file::open_file(open_mode_t const mode, error_code& ec)
	{
		if (m_file && m_file->is_open()
			&& (mode == open_mode::read_only
			|| (m_file->open_mode() & open_mode::rw_mask) == open_mode::read_write))
			return;

		std::string const fn = combine_path(m_path, m_name);
		auto f = std::make_shared<file>(fn, mode, ec);
		if (((mode & open_mode::rw_mask) != open_mode::read_only)
			&& ec == boost::system::errc::no_such_file_or_directory)
		{
			// this means the directory the file is in doesn't exist.
			// so create it
			ec.clear();
			create_directories(m_path, ec);

			if (ec) return;
			f = std::make_shared<file>(fn, mode, ec);
		}
		if (!ec) m_file = std::move(f);
	}

	void part_file::free_piece(piece_index_t const piece)
	{
		std::lock_guard<std::mutex> l(m_mutex);

		auto const i = m_piece_map.find(piece);
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
		std::lock_guard<std::mutex> l(m_mutex);

		flush_metadata_impl(ec);
		if (ec) return;

		// we're only supposed to move part files from a fence job. i.e. no other
		// disk jobs are supposed to be in-flight at this point
		TORRENT_ASSERT(!m_file || m_file.use_count() == 1);
		m_file.reset();

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

	void part_file::export_file(std::function<void(std::int64_t, span<char>)> f
		, std::int64_t const offset, std::int64_t size, error_code& ec)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		piece_index_t piece(int(offset / m_piece_size));
		piece_index_t const end = piece_index_t(int(((offset + size) + m_piece_size - 1) / m_piece_size));

		std::unique_ptr<char[]> buf;

		std::int64_t piece_offset = offset - std::int64_t(static_cast<int>(piece))
			* m_piece_size;
		std::int64_t file_offset = 0;
		for (; piece < end; ++piece)
		{
			auto const i = m_piece_map.find(piece);
			int const block_to_copy = int(std::min(m_piece_size - piece_offset, size));
			if (i != m_piece_map.end())
			{
				slot_index_t const slot = i->second;
				open_file(open_mode::read_only, ec);
				if (ec) return;
				auto const local_file = m_file;

				if (!buf) buf.reset(new char[std::size_t(m_piece_size)]);

				// don't hold the lock during disk I/O
				l.unlock();

				iovec_t v = {buf.get(), block_to_copy};
				auto bytes_read = local_file->readv(slot_offset(slot) + piece_offset, v, ec);
				v = v.first(static_cast<std::ptrdiff_t>(bytes_read));
				TORRENT_ASSERT(!ec);
				if (ec || v.empty()) return;

				f(file_offset, {buf.get(), block_to_copy});

				// we're done with the disk I/O, grab the lock again to update
				// the slot map
				l.lock();

				if (block_to_copy == m_piece_size)
				{
					// since we released the lock, it's technically possible that
					// another thread removed this slot map entry, and invalidated
					// our iterator. Now that we hold the lock again, perform
					// another lookup to be sure.
					auto const j = m_piece_map.find(piece);
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
		std::lock_guard<std::mutex> l(m_mutex);

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
			m_file.reset();

			// if we don't have any pieces left in the
			// part file, remove it
			std::string const p = combine_path(m_path, m_name);
			remove(p, ec);

			if (ec == boost::system::errc::no_such_file_or_directory)
				ec.clear();
			return;
		}

		open_file(open_mode::read_write | open_mode::attribute_hidden, ec);
		if (ec) return;

		std::vector<char> header(static_cast<std::size_t>(m_header_size));

		using namespace libtorrent::detail;

		char* ptr = header.data();
		write_uint32(m_max_pieces, ptr);
		write_uint32(m_piece_size, ptr);

		for (piece_index_t piece(0); piece < piece_index_t(m_max_pieces); ++piece)
		{
			auto const i = m_piece_map.find(piece);
			slot_index_t const slot(i == m_piece_map.end()
				? slot_index_t(-1) : i->second);
			write_int32(static_cast<int>(slot), ptr);
		}
		std::memset(ptr, 0, std::size_t(m_header_size - (ptr - header.data())));
		iovec_t b = header;
		m_file->writev(0, b, ec);
		if (ec) return;
		m_dirty_metadata = false;
	}
}
