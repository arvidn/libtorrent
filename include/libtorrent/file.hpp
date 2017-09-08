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

#include <boost/noncopyable.hpp>

#ifdef TORRENT_WINDOWS
// windows part
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <sys/types.h>
#else
// posix part
#define _FILE_OFFSET_BITS 64

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

#include "libtorrent/aux_/max_path.hpp" // for TORRENT_MAX_PATH

#undef _FILE_OFFSET_BITS

#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent {

#ifdef TORRENT_WINDOWS
	using handle_type = HANDLE;
#else
	using handle_type = int;
#endif

	class TORRENT_EXTRA_EXPORT directory : public boost::noncopyable
	{
	public:
		directory(std::string const& path, error_code& ec);
		~directory();
		void next(error_code& ec);
		std::string file() const;
		std::uint64_t inode() const;
		bool done() const { return m_done; }
	private:
#ifdef TORRENT_WINDOWS
		HANDLE m_handle;
		int m_inode;
		WIN32_FIND_DATAW m_fd;
#else
		DIR* m_handle;
#ifdef TORRENT_ANDROID
// this is due to a documented bug in android related to a wrong type
// of ino_t, for general discussion and internal changes see:
// https://issuetracker.google.com/issues/37011207 - for general discussion
// https://android-review.googlesource.com/#/c/platform/system/core/+/123482/
		std::uint64_t m_inode;
#else
		ino_t m_inode;
#endif // TORRENT_ANDROID
		std::string m_name;
#endif
		bool m_done;
	};

	struct TORRENT_EXTRA_EXPORT file : boost::noncopyable
	{
		file();
		file(std::string const& p, aux::open_mode_t m, error_code& ec);
		~file();

		bool open(std::string const& p, aux::open_mode_t m, error_code& ec);
		bool is_open() const;
		void close();
		bool set_size(std::int64_t size, error_code& ec);

		aux::open_mode_t open_mode() const { return m_open_mode; }

		std::int64_t writev(std::int64_t file_offset, span<iovec_t const> bufs
			, error_code& ec, aux::open_mode_t flags = {});
		std::int64_t readv(std::int64_t file_offset, span<iovec_t const> bufs
			, error_code& ec, aux::open_mode_t flags = {});

		std::int64_t get_size(error_code& ec) const;

		// return the offset of the first byte that
		// belongs to a data-region
		std::int64_t sparse_end(std::int64_t start) const;

		handle_type native_handle() const { return m_file_handle; }

	private:

		handle_type m_file_handle;

		aux::open_mode_t m_open_mode{};
#if defined TORRENT_WINDOWS
		static bool has_manage_volume_privs;
#endif
	};
}

#endif // TORRENT_FILE_HPP_INCLUDED
