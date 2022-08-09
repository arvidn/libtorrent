/*

Copyright (c) 2004, 2008-2010, 2014-2018, 2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
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

#ifndef TORRENT_FILE_HPP_INCLUDED
#define TORRENT_FILE_HPP_INCLUDED

#include <memory>
#include <string>
#include <functional>

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/flags.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_WINDOWS
// windows part
#include "libtorrent/aux_/windows.hpp"
#include <winioctl.h>
#include <sys/types.h>
#else
// posix part

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h> // for DIR

#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent {

#ifdef TORRENT_WINDOWS
	using handle_type = HANDLE;
	const handle_type invalid_handle = INVALID_HANDLE_VALUE;
#else
	using handle_type = int;
	const handle_type invalid_handle = -1;
#endif

namespace aux {

	int pwrite_all(handle_type handle
		, span<char const> buf
		, std::int64_t file_offset
		, error_code& ec);

	int pread_all(handle_type handle
		, span<char> buf
		, std::int64_t file_offset
		, error_code& ec);

	struct TORRENT_EXTRA_EXPORT file_handle
	{
		file_handle(): m_fd(invalid_handle) {}
		file_handle(string_view name, std::int64_t size, open_mode_t mode);
		file_handle(file_handle const& rhs) = delete;
		file_handle& operator=(file_handle const& rhs) = delete;

		file_handle(file_handle&& rhs) : m_fd(rhs.m_fd) { rhs.m_fd = invalid_handle; }
		file_handle& operator=(file_handle&& rhs) &;

		~file_handle();

		std::int64_t get_size() const;

		handle_type fd() const { return m_fd; }
	private:
		void close();
		handle_type m_fd;
#ifdef TORRENT_WINDOWS
		aux::open_mode_t m_open_mode;
#endif
	};

} // namespace aux
}

#endif // TORRENT_FILE_HPP_INCLUDED
