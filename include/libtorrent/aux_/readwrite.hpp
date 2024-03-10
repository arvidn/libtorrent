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

template <typename Char, typename Fun>
int readwrite_impl(file_storage const& files, span<Char> buf
	, piece_index_t const piece, const int offset
	, storage_error& ec, Fun op)
{
	TORRENT_ASSERT(piece >= piece_index_t(0));
	TORRENT_ASSERT(piece < files.end_piece());
	TORRENT_ASSERT(offset >= 0);
	TORRENT_ASSERT(buf.size() > 0);

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

}

#endif
