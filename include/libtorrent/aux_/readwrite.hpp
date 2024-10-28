/*

Copyright (c) 2017-2020, 2022, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_READWRITE_HPP_INCLUDE
#define TORRENT_READWRITE_HPP_INCLUDE

#include <cstdint>
#include <functional>

#include "libtorrent/units.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/alloca.hpp"

namespace libtorrent::aux {

// This function is responsible for turning read and write operations in the
// torrent space (pieces) into read and write operations in the filesystem
// space (files on disk).
//
// Much of what needs to be done when reading and writing is buffer
// management and piece to file mapping. Most of that is the same for reading
// and writing. This function is a template, and the fileop decides what to
// do with the file and the buffers.
//
// op is a read or write operation so that readwrite() knows
// what to do when it's actually touching the file

template <typename Fun>
int readwrite(file_storage const& files, span<char> buf
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	return readwrite_impl(files, buf, piece, offset, ec, op);
}

template <typename Fun>
int readwrite(file_storage const& files, span<char const> buf
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	return readwrite_impl(files, buf, piece, offset, ec, op);
}

template <typename Fun>
int readwrite_vec(file_storage const& files, span<span<char> const> bufs
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	return readwrite_vec_impl(files, bufs, piece, offset, ec, op);
}

template <typename Fun>
int readwrite_vec(file_storage const& files, span<span<char const> const> bufs
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	return readwrite_vec_impl(files, bufs, piece, offset, ec, op);
}

#if TORRENT_USE_ASSERTS
namespace {

	inline int count_bufs(span<span<char const> const> bufs, int bytes)
	{
		std::ptrdiff_t size = 0;
		int count = 0;
		if (bytes == 0) return count;
		for (auto b : bufs)
		{
			++count;
			size += b.size();
			if (size >= bytes) return count;
		}
		return count;
	}

}
#endif

template <typename Char, typename Fun>
int readwrite_impl(file_storage const& files, span<Char> buf
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	TORRENT_ASSERT(piece >= piece_index_t(0));
	TORRENT_ASSERT(piece < files.end_piece());
	TORRENT_ASSERT(offset >= 0);
	TORRENT_ASSERT(buf.size() > 0);

	TORRENT_ASSERT(static_cast<int>(piece) * static_cast<std::int64_t>(files.piece_length())
		+ offset + buf.size() <= files.total_size());

	// find the file iterator and file offset
	std::int64_t const torrent_offset = static_cast<int>(piece) * std::int64_t(files.piece_length()) + offset;
	file_index_t file_index = files.file_index_at_offset(torrent_offset);
	TORRENT_ASSERT(torrent_offset >= files.file_offset(file_index));
	TORRENT_ASSERT(torrent_offset < files.file_offset(file_index) + files.file_size(file_index));
	std::int64_t file_offset = torrent_offset - files.file_offset(file_index);

	int ret = 0;

	while (buf.size() > 0)
	{
		// the number of bytes left to read in the current file (specified by
		// file_index). This is the minimum of (file_size - file_offset) and
		// buf.size().
		int file_bytes_left = int(buf.size());
		if (file_offset + file_bytes_left > files.file_size(file_index))
			file_bytes_left = std::max(static_cast<int>(files.file_size(file_index) - file_offset), 0);

		// there are no bytes left in this file, move to the next one
		// this loop skips over empty files
		if (file_bytes_left == 0)
		{
			do
			{
				++file_index;
				file_offset = 0;
				TORRENT_ASSERT(file_index < files.end_file());

				// this should not happen. buf.size() should be clamped by the total
				// size of the torrent, so we should never run off the end of it
				if (file_index >= files.end_file()) return ret;

				// skip empty files
			}
			while (files.file_size(file_index) == 0);

			file_bytes_left = int(buf.size());
			if (file_offset + file_bytes_left > files.file_size(file_index))
				file_bytes_left = std::max(static_cast<int>(files.file_size(file_index) - file_offset), 0);
			TORRENT_ASSERT(file_bytes_left > 0);
		}

		int const bytes_transferred = op(file_index, file_offset
			, buf.first(file_bytes_left), ec);
		TORRENT_ASSERT(bytes_transferred <= file_bytes_left);
		if (ec)
		{
			ec.file(file_index);
			return ret;
		}

		buf = buf.subspan(bytes_transferred);
		file_offset += bytes_transferred;
		ret += bytes_transferred;

		// if the file operation returned 0, we've hit end-of-file. We're done
		if (bytes_transferred == 0 && file_bytes_left > 0 )
		{
			// fill in this information in case the caller wants to treat
			// a short-read as an error
			ec.operation = operation_t::file_read;
			ec.ec = boost::asio::error::eof;
			ec.file(file_index);
		}
	}
	return ret;
}

template <typename Char>
inline span<span<Char>> advance_bufs(span<span<Char>> bufs, int const bytes)
{
	TORRENT_ASSERT(bytes >= 0);
	std::ptrdiff_t size = 0;
	for (;;)
	{
		size += bufs.front().size();
		if (size >= bytes)
		{
			bufs.front() = bufs.front().last(size - bytes);
			return bufs;
		}
		bufs = bufs.subspan(1);
	}
	return bufs;
}

template <typename Char>
int copy_bufs(span<span<Char> const> src_bufs, int bytes
	, span<span<Char>> target)
{
	TORRENT_ASSERT(bytes >= 0);
	auto dst = target.begin();
	int ret = 0;
	if (bytes == 0) return ret;
	for (span<Char> const& src : src_bufs)
	{
		auto const to_copy = std::min(src.size(), std::ptrdiff_t(bytes));
		*dst = src.first(to_copy);
		bytes -= int(to_copy);
		++ret;
		++dst;
		if (bytes <= 0) return ret;
	}
	return ret;
}

template <typename Char>
int bufs_size(span<span<Char> const> bufs)
{
	std::ptrdiff_t size = 0;
	for (auto buf : bufs) size += buf.size();
	return int(size);
}

template <typename Char, typename Fun>
int readwrite_vec_impl(file_storage const& files, span<span<Char> const> bufs
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	TORRENT_ASSERT(piece >= piece_index_t(0));
	TORRENT_ASSERT(piece < files.end_piece());
	TORRENT_ASSERT(offset >= 0);
	TORRENT_ASSERT(bufs.size() > 0);

	const int size = bufs_size(bufs);
	TORRENT_ASSERT(size > 0);

	TORRENT_ASSERT(static_cast<int>(piece) * static_cast<std::int64_t>(files.piece_length())
		+ offset + size <= files.total_size());

	// find the file iterator and file offset
	std::int64_t const torrent_offset = static_cast<int>(piece) * std::int64_t(files.piece_length()) + offset;
	file_index_t file_index = files.file_index_at_offset(torrent_offset);
	TORRENT_ASSERT(torrent_offset >= files.file_offset(file_index));
	TORRENT_ASSERT(torrent_offset < files.file_offset(file_index) + files.file_size(file_index));
	std::int64_t file_offset = torrent_offset - files.file_offset(file_index);

	// the number of bytes left before this read or write operation is
	// completely satisfied.
	int bytes_left = size;

	TORRENT_ASSERT(bytes_left >= 0);

	// copy the iovec array so we can use it to keep track of our current
	// location by updating the head base pointer and size. (see
	// advance_bufs())

	// TODO: is it really necessary to make two copies of the iovecs? The low-level
	// system call will need to make its own copy as well
	TORRENT_ALLOCA(current_buf, span<Char>, bufs.size());
	copy_bufs(bufs, size, current_buf);
	TORRENT_ASSERT(count_bufs(current_buf, size) == int(bufs.size()));

	TORRENT_ALLOCA(tmp_buf, span<Char>, bufs.size());
	while (bytes_left > 0)
	{
		// the number of bytes left to read in the current file (specified by
		// file_index). This is the minimum of (file_size - file_offset) and
		// bytes_left.
		int file_bytes_left = bytes_left;
		if (file_offset + file_bytes_left > files.file_size(file_index))
			file_bytes_left = std::max(static_cast<int>(files.file_size(file_index) - file_offset), 0);

		// there are no bytes left in this file, move to the next one
		// this loop skips over empty files
		while (file_bytes_left == 0)
		{
			++file_index;
			file_offset = 0;
			TORRENT_ASSERT(file_index < files.end_file());

			// this should not happen. bytes_left should be clamped by the total
			// size of the torrent, so we should never run off the end of it
			if (file_index >= files.end_file()) return size;

			file_bytes_left = bytes_left;

			if (file_offset + file_bytes_left > files.file_size(file_index))
				file_bytes_left = std::max(static_cast<int>(files.file_size(file_index) - file_offset), 0);
		}

		// make a copy of the iovec array that _just_ covers the next
		// file_bytes_left bytes, i.e. just this one operation
		int const tmp_bufs_used = copy_bufs(span<span<char const> const>(current_buf), file_bytes_left, tmp_buf);

		int const bytes_transferred = op(file_index, file_offset
			, tmp_buf.first(tmp_bufs_used), ec);
		TORRENT_ASSERT(bytes_transferred <= file_bytes_left);
		if (ec)
		{
			ec.file(file_index);
			return -1;
		}

		// advance our position in the iovec array and the file offset.
		current_buf = advance_bufs(current_buf, bytes_transferred);
		bytes_left -= bytes_transferred;
		file_offset += bytes_transferred;
		TORRENT_ASSERT(count_bufs(current_buf, bytes_left) <= int(bufs.size()));

		// if the file operation returned 0, we've hit end-of-file. We're done
		if (bytes_transferred == 0)
		{
			if (file_bytes_left > 0 )
			{
				// fill in this information in case the caller wants to treat
				// a short-read as an error
				ec.operation = operation_t::file_read;
				ec.ec = boost::asio::error::eof;
				ec.file(file_index);
			}
			return size - bytes_left;
		}
	}
	return size;
}
}

#endif
