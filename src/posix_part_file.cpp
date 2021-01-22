/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016-2017, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2018, d-komarov
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

  The posix_part_file file format is an array of piece sized blocks with
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
  // A value of 0xffffffff (-1 if you will) means the piece is not in the posix_part_file
  // Any other value means the piece resides in the slot with that index
  uint32_t piece[num_pieces];

  // unused, n is defined as the number to align the size of this
  // header to an even multiple of 1024 bytes.
  uint8_t padding[n];

*/

#include "libtorrent/aux_/posix_part_file.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t

#include <functional> // for std::function
#include <cstdint>

namespace {

	// round up to even kilobyte
	int round_up(int n)
	{ return (n + 1023) & ~0x3ff; }
}

#ifdef _MSC_VER
#define fseeko _fseeki64
#endif

namespace libtorrent {
namespace aux {

	posix_part_file::posix_part_file(std::string path, std::string name
		, int const num_pieces, int const piece_size)
		: m_path(std::move(path))
		, m_name(std::move(name))
		, m_max_pieces(num_pieces)
		, m_piece_size(piece_size)
		, m_header_size(round_up((2 + num_pieces) * 4))
	{
		TORRENT_ASSERT(num_pieces > 0);
		TORRENT_ASSERT(m_piece_size > 0);

		error_code ec;
		auto f = open_file(open_mode::read_only, ec);
		if (ec) return;

		// parse header
		std::vector<char> header(static_cast<std::size_t>(m_header_size));
		auto const n = std::fread(header.data(), 1, header.size(), f.file());
		if (std::size_t(n) != header.size())
		{
			ec.assign(errno, generic_category());
			return;
		}

		// we don't have a full header. consider the file empty
		if (n < std::size_t(m_header_size)) return;
		using namespace libtorrent::aux;

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
	}

	posix_part_file::~posix_part_file()
	{
		error_code ec;
		flush_metadata_impl(ec);
	}

	slot_index_t posix_part_file::allocate_slot(piece_index_t const piece)
	{
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

	int posix_part_file::writev(span<iovec_t const> bufs, piece_index_t const piece
		, int const offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(int(bufs.size()) + offset <= m_piece_size);

		auto f = open_file(open_mode::read_write, ec);
		if (ec) return -1;

		auto const i = m_piece_map.find(piece);
		slot_index_t const slot = (i == m_piece_map.end())
			? allocate_slot(piece) : i->second;

		if (::fseeko(f.file(), slot_offset(slot) + offset, SEEK_SET) != 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		int ret = 0;
		for (auto const& b : bufs)
		{
			auto const written = std::fwrite(b.data(), 1, std::size_t(b.size()), f.file());
			if (written != std::size_t(b.size()))
			{
				ec.assign(errno, generic_category());
				return -1;
			}
			ret += int(written);
		}
		return ret;
	}

	int posix_part_file::readv(span<iovec_t const> bufs
		, piece_index_t const piece
		, int const offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(int(bufs.size()) + offset <= m_piece_size);

		auto const i = m_piece_map.find(piece);
		if (i == m_piece_map.end())
		{
			ec = make_error_code(boost::system::errc::no_such_file_or_directory);
			return -1;
		}

		slot_index_t const slot = i->second;

		auto f = open_file(open_mode::read_only, ec);
		if (ec) return -1;

		if (::fseeko(f.file(), slot_offset(slot) + offset, SEEK_SET) != 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		int ret = 0;
		for (auto const& b : bufs)
		{
			auto const read = std::fread(b.data(), 1, std::size_t(b.size()), f.file());
			if (read != std::size_t(b.size()))
			{
				ec.assign(errno, generic_category());
				return -1;
			}
			ret += int(read);
		}
		return ret;
	}

	int posix_part_file::hashv(hasher& ph
		, std::ptrdiff_t const len
		, piece_index_t const piece
		, int const offset, error_code& ec)
	{
		return do_hashv(ph, len, piece, offset, ec);
	}

	int posix_part_file::hashv2(hasher256& ph
		, std::ptrdiff_t const len
		, piece_index_t const piece
		, int const offset, error_code& ec)
	{
		return do_hashv(ph, len, piece, offset, ec);
	}

	template <typename Hasher>
	int posix_part_file::do_hashv(Hasher& ph
		, std::ptrdiff_t const len
		, piece_index_t const piece
		, int const offset, error_code& ec)
	{
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(len >= 0);
		TORRENT_ASSERT(int(len) + offset <= m_piece_size);

		auto const i = m_piece_map.find(piece);
		if (i == m_piece_map.end())
		{
			ec = error_code(boost::system::errc::no_such_file_or_directory
				, boost::system::generic_category());
			return -1;
		}

		slot_index_t const slot = i->second;
		auto f = open_file(open_mode::read_only, ec);
		if (ec) return -1;

		std::vector<char> buffer(static_cast<std::size_t>(len));
		std::int64_t const slot_offset = std::int64_t(m_header_size) + std::int64_t(static_cast<int>(slot)) * m_piece_size;
		if (::fseeko(f.file(), slot_offset + offset, SEEK_SET) != 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		auto const ret = std::fread(buffer.data(), 1, buffer.size(), f.file());
		if (ret != buffer.size())
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		ph.update(buffer);
		return numeric_cast<int>(ret);
	}

	file_pointer posix_part_file::open_file(open_mode const mode, error_code& ec)
	{
		std::string const fn = combine_path(m_path, m_name);
#ifdef TORRENT_WINDOWS
		wchar_t const* mode_str[] = {L"rb", L"rb+"};
		file_pointer ret(::_wfopen(convert_to_native_path_string(fn).c_str()
			, mode_str[static_cast<std::uint8_t>(mode)]));
#else
		char const* mode_str[] = {"rb", "rb+"};
		file_pointer ret(::fopen(fn.c_str()
			, mode_str[static_cast<std::uint8_t>(mode)]));
#endif
		if (ret.file() == nullptr)
			ec.assign(errno, generic_category());

		if (mode == open_mode::read_write
			&& ec == boost::system::errc::no_such_file_or_directory)
		{
			// this means the directory the file is in doesn't exist.
			// so create it
			ec.clear();
			create_directories(m_path, ec);

			if (ec) return {};

#ifdef TORRENT_WINDOWS
			ret = file_pointer(::_wfopen(convert_to_native_path_string(fn).c_str(), L"wb+"));
#else
			ret = file_pointer(::fopen(fn.c_str(), "wb+"));
#endif
			if (ret.file() == nullptr)
				ec.assign(errno, generic_category());
		}
		if (ec) return {};
		return ret;
	}

	void posix_part_file::free_piece(piece_index_t const piece)
	{
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

	void posix_part_file::move_partfile(std::string const& path, error_code& ec)
	{
		flush_metadata_impl(ec);
		if (ec) return;

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

	void posix_part_file::export_file(std::function<void(std::int64_t, span<char>)> f
		, std::int64_t const offset, std::int64_t size, error_code& ec)
	{
		// there's nothing stored in the posix_part_file. Nothing to do
		if (m_piece_map.empty()) return;

		piece_index_t piece(int(offset / m_piece_size));
		piece_index_t const end = piece_index_t(int(((offset + size) + m_piece_size - 1) / m_piece_size));

		std::unique_ptr<char[]> buf;

		std::int64_t piece_offset = offset - std::int64_t(static_cast<int>(piece))
			* m_piece_size;
		std::int64_t file_offset = 0;
		auto file = open_file(open_mode::read_only, ec);
		if (ec) return;

		for (; piece < end; ++piece)
		{
			auto const i = m_piece_map.find(piece);
			int const block_to_copy = int(std::min(m_piece_size - piece_offset, size));
			if (i != m_piece_map.end())
			{
				slot_index_t const slot = i->second;

				if (!buf) buf.reset(new char[std::size_t(m_piece_size)]);

				if (::fseeko(file.file(), slot_offset(slot) + piece_offset, SEEK_SET) != 0)
				{
					ec.assign(errno, generic_category());
					return;
				}
				auto bytes_read = std::fread(buf.get(), 1, std::size_t(block_to_copy), file.file());
				if (int(bytes_read) != block_to_copy)
					ec.assign(errno, generic_category());

				TORRENT_ASSERT(!ec);
				if (ec) return;

				f(file_offset, {buf.get(), block_to_copy});
			}
			file_offset += block_to_copy;
			piece_offset = 0;
			size -= block_to_copy;
		}
	}

	void posix_part_file::flush_metadata(error_code& ec)
	{
		flush_metadata_impl(ec);
	}

	// TODO: instead of rebuilding the whole file header
	// and flushing it, update the slot entries as we go
	void posix_part_file::flush_metadata_impl(error_code& ec)
	{
		// do we need to flush the metadata?
		if (m_dirty_metadata == false) return;

		if (m_piece_map.empty())
		{
			// if we don't have any pieces left in the
			// part file, remove it
			std::string const p = combine_path(m_path, m_name);
			remove(p, ec);

			if (ec == boost::system::errc::no_such_file_or_directory)
				ec.clear();
			return;
		}

		auto f = open_file(open_mode::read_write, ec);
		if (ec) return;

		std::vector<char> header(static_cast<std::size_t>(m_header_size));

		using namespace libtorrent::aux;

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

		auto const written = std::fwrite(header.data(), 1, header.size(), f.file());
		if (written != header.size())
		{
			ec.assign(errno, generic_category());
			return;
		}
		m_dirty_metadata = false;
	}
}
}
