/*

Copyright (c) 2016, 2018-2022, Arvid Norberg
Copyright (c) 2019, Steven Siloti
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

#ifndef TORRENT_MMAP_HPP
#define TORRENT_MMAP_HPP

#include "libtorrent/config.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

#include "libtorrent/disk_interface.hpp" // for open_file_state
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/file.hpp" // for file_handle

#if TORRENT_HAVE_MAP_VIEW_OF_FILE

#include "libtorrent/aux_/windows.hpp"
#include <mutex>

#endif // TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace libtorrent {

// for now
using byte = char;

namespace aux {

	// files smaller than this will not be mapped into memory, they will just
	// have a file descriptor to be used with regular pread/pwrite calls
	std::int64_t const mapped_file_cutoff = 1024 * 1024;

	using namespace flags;

#if TORRENT_HAVE_MAP_VIEW_OF_FILE
	struct TORRENT_EXTRA_EXPORT file_mapping_handle
	{
		file_mapping_handle(file_handle file, open_mode_t mode, std::int64_t size);
		~file_mapping_handle();
		file_mapping_handle(file_mapping_handle const&) = delete;
		file_mapping_handle& operator=(file_mapping_handle const&) = delete;

		file_mapping_handle(file_mapping_handle&& fm);
		file_mapping_handle& operator=(file_mapping_handle&& fm) &;

		HANDLE handle() const { return m_mapping; }
		handle_type fd() const { return m_file.fd(); }
	private:
		void close();
		file_handle m_file;
		HANDLE m_mapping;
	};
#endif

	struct TORRENT_EXTRA_EXPORT file_mapping : std::enable_shared_from_this<file_mapping>
	{
		file_mapping(file_handle file, open_mode_t mode, std::int64_t file_size
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, std::shared_ptr<std::mutex> open_unmap_lock
#endif
			);

#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		void flush();
#endif

		// non-copyable
		file_mapping(file_mapping const&) = delete;
		file_mapping& operator=(file_mapping const&) = delete;

		file_mapping(file_mapping&& rhs);
		file_mapping& operator=(file_mapping&& rhs) &;
		~file_mapping();

		handle_type fd() const { return m_file.fd(); }

		bool has_memory_map() const { return m_mapping != nullptr; }

		// the memory range this file has been mapped into
		span<byte> range()
		{
			TORRENT_ASSERT(m_mapping);
			return { static_cast<byte*>(m_mapping), static_cast<std::ptrdiff_t>(m_size) };
		}

		// hint the kernel that we probably won't need this part of the file
		// anytime soon
		void dont_need(span<byte const> range);

		// hint the kernel that the given (dirty) range of pages should be
		// flushed to disk
		void page_out(span<byte const> range);

	private:

		void close();

		std::int64_t m_size;
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		file_mapping_handle m_file;
		std::shared_ptr<std::mutex> m_open_unmap_lock;
#else
		file_handle m_file;
#endif
		void* m_mapping;
	};
} // aux
} // libtorrent

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

#endif

