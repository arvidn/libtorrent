/*

Copyright (c) 2003-2018, Arvid Norberg
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
#include "libtorrent/flags.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/noncopyable.hpp>

#ifdef TORRENT_WINDOWS
// windows part
#include "libtorrent/aux_/windows.hpp"
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
		bool done() const { return m_done; }
	private:
#ifdef TORRENT_WINDOWS
		HANDLE m_handle;
		WIN32_FIND_DATAW m_fd;
#else
		DIR* m_handle;
		std::string m_name;
#endif
		bool m_done;
	};

	struct file;

	using file_handle = std::shared_ptr<file>;

	// hidden
	using open_mode_t = flags::bitfield_flag<std::uint32_t, struct open_mode_tag>;

	// the open mode for files. Used for the file constructor or
	// file::open().
	namespace open_mode {

		// open the file for reading only
		constexpr open_mode_t read_only{};

		// open the file for writing only
		constexpr open_mode_t write_only = 0_bit;

		// open the file for reading and writing
		constexpr open_mode_t read_write = 1_bit;

		// the mask for the bits making up the read-write mode.
		constexpr open_mode_t rw_mask = read_only | write_only | read_write;

		// open the file in sparse mode (if supported by the
		// filesystem).
		constexpr open_mode_t sparse = 2_bit;

		// don't update the access timestamps on the file (if
		// supported by the operating system and filesystem).
		// this generally improves disk performance.
		constexpr open_mode_t no_atime = 3_bit;

		// open the file for random access. This disables read-ahead
		// logic
		constexpr open_mode_t random_access = 4_bit;

		// don't put any pressure on the OS disk cache
		// because of access to this file. We expect our
		// files to be fairly large, and there is already
		// a cache at the bittorrent block level. This
		// may improve overall system performance by
		// leaving running applications in the page cache
		constexpr open_mode_t no_cache = 5_bit;

		// this is only used for readv/writev flags
		constexpr open_mode_t coalesce_buffers = 6_bit;

		// when creating a file, set the hidden attribute (windows only)
		constexpr open_mode_t attribute_hidden = 7_bit;

		// when creating a file, set the executable attribute
		constexpr open_mode_t attribute_executable = 8_bit;

		// the mask of all attribute bits
		constexpr open_mode_t attribute_mask = attribute_hidden | attribute_executable;
	}

	struct TORRENT_EXTRA_EXPORT file : boost::noncopyable
	{
		file();
		file(std::string const& p, open_mode_t m, error_code& ec);
		~file();

		bool open(std::string const& p, open_mode_t m, error_code& ec);
		bool is_open() const;
		void close();
		bool set_size(std::int64_t size, error_code& ec);

		open_mode_t open_mode() const { return m_open_mode; }

		std::int64_t writev(std::int64_t file_offset, span<iovec_t const> bufs
			, error_code& ec, open_mode_t flags = open_mode_t{});
		std::int64_t readv(std::int64_t file_offset, span<iovec_t const> bufs
			, error_code& ec, open_mode_t flags = open_mode_t{});

		std::int64_t get_size(error_code& ec) const;

		handle_type native_handle() const { return m_file_handle; }

	private:

		handle_type m_file_handle;

		open_mode_t m_open_mode{};
	};
}

#endif // TORRENT_FILE_HPP_INCLUDED
