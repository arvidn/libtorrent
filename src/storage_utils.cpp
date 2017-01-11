/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/file.hpp" // for count_bufs

namespace libtorrent
{
	int copy_bufs(span<iovec_t const> bufs, int bytes, span<iovec_t> target)
	{
		int size = 0;
		for (int i = 0;; i++)
		{
			target[i] = bufs[i];
			size += int(bufs[i].iov_len);
			if (size >= bytes)
			{
				target[i].iov_len -= size - bytes;
				return i + 1;
			}
		}
	}

	span<iovec_t> advance_bufs(span<iovec_t> bufs, int bytes)
	{
		int size = 0;
		for (;;)
		{
			size += int(bufs.front().iov_len);
			if (size >= bytes)
			{
				bufs.front().iov_base = reinterpret_cast<char*>(bufs.front().iov_base)
					+ bufs.front().iov_len - (size - bytes);
				bufs.front().iov_len = size - bytes;
				return bufs;
			}
			bufs = bufs.subspan(1);
		}
	}

#if TORRENT_USE_ASSERTS
	namespace {

	int count_bufs(span<iovec_t const> bufs, int bytes)
	{
		int size = 0;
		int count = 1;
		if (bytes == 0) return 0;
		for (auto i = bufs.begin();; ++i, ++count)
		{
			size += int(i->iov_len);
			if (size >= bytes) return count;
		}
	}

	}
#endif

	// much of what needs to be done when reading and writing is buffer
	// management and piece to file mapping. Most of that is the same for reading
	// and writing. This function is a template, and the fileop decides what to
	// do with the file and the buffers.
	int readwritev(file_storage const& files, span<iovec_t const> const bufs
		, piece_index_t const piece, const int offset, fileop& op
		, storage_error& ec)
	{
		TORRENT_ASSERT(piece >= piece_index_t(0));
		TORRENT_ASSERT(piece < files.end_piece());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(bufs.size() > 0);

		const int size = bufs_size(bufs);
		TORRENT_ASSERT(size > 0);

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
		TORRENT_ALLOCA(current_buf, iovec_t, bufs.size());
		copy_bufs(bufs, size, current_buf);
		TORRENT_ASSERT(count_bufs(current_buf, size) == int(bufs.size()));

		TORRENT_ALLOCA(tmp_buf, iovec_t, bufs.size());

		// the number of bytes left to read in the current file (specified by
		// file_index). This is the minimum of (file_size - file_offset) and
		// bytes_left.
		int file_bytes_left;

		while (bytes_left > 0)
		{
			file_bytes_left = bytes_left;
			if (file_offset + file_bytes_left > files.file_size(file_index))
				file_bytes_left = (std::max)(static_cast<int>(files.file_size(file_index) - file_offset), 0);

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
					file_bytes_left = (std::max)(static_cast<int>(files.file_size(file_index) - file_offset), 0);
			}

			// make a copy of the iovec array that _just_ covers the next
			// file_bytes_left bytes, i.e. just this one operation
			int tmp_bufs_used = copy_bufs(current_buf, file_bytes_left, tmp_buf);

			int bytes_transferred = op.file_op(file_index, file_offset
				, tmp_buf.first(tmp_bufs_used), ec);
			if (ec) return -1;

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
					ec.file(file_index);
				}
				return size - bytes_left;
			}
		}
		return size;
	}

}

