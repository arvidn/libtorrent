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

#ifndef TORRENT_PATH_HPP_INCLUDED
#define TORRENT_PATH_HPP_INCLUDED

#include <memory>
#include <string>
#include <functional>

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t

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

	struct file_status
	{
		std::int64_t file_size = 0;
		std::uint64_t atime = 0;
		std::uint64_t mtime = 0;
		std::uint64_t ctime = 0;
		enum {
#if defined TORRENT_WINDOWS
			fifo = 0x1000, // named pipe (fifo)
			character_special = 0x2000,  // character special
			directory = 0x4000,  // directory
			regular_file = 0x8000  // regular
#else
			fifo = 0010000, // named pipe (fifo)
			character_special = 0020000,  // character special
			directory = 0040000,  // directory
			block_special = 0060000,  // block special
			regular_file = 0100000,  // regular
			link = 0120000,  // symbolic link
			socket = 0140000  // socket
#endif
		} modes_t;
		int mode = 0;
	};

	// internal flags for stat_file
	enum { dont_follow_links = 1 };
	TORRENT_EXTRA_EXPORT void stat_file(std::string const& f, file_status* s
		, error_code& ec, int flags = 0);
	TORRENT_EXTRA_EXPORT void rename(std::string const& f
		, std::string const& newf, error_code& ec);
	TORRENT_EXTRA_EXPORT void create_directories(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void create_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void remove_all(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void remove(std::string const& f, error_code& ec);
	TORRENT_EXTRA_EXPORT bool exists(std::string const& f, error_code& ec);
	TORRENT_EXTRA_EXPORT bool exists(std::string const& f);
	TORRENT_EXTRA_EXPORT std::int64_t file_size(std::string const& f);
	TORRENT_EXTRA_EXPORT bool is_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void recursive_copy(std::string const& old_path
		, std::string const& new_path, error_code& ec);
	TORRENT_EXTRA_EXPORT void copy_file(std::string const& f
		, std::string const& newf, error_code& ec);
	TORRENT_EXTRA_EXPORT void move_file(std::string const& f
		, std::string const& newf, error_code& ec);

	// file is expected to exist, link will be created to point to it. If hard
	// links are not supported by the filesystem or OS, the file will be copied.
	TORRENT_EXTRA_EXPORT void hard_link(std::string const& file
		, std::string const& link, error_code& ec);

	// split out a path segment from the left side or right side
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> rsplit_path(string_view p);
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> lsplit_path(string_view p);
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> lsplit_path(string_view p, std::size_t pos);

	TORRENT_EXTRA_EXPORT std::string extension(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string remove_extension(std::string const& f);
	TORRENT_EXTRA_EXPORT bool is_root_path(std::string const& f);
	TORRENT_EXTRA_EXPORT bool compare_path(std::string const& lhs, std::string const& rhs);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string parent_path(std::string const& f);
	TORRENT_EXTRA_EXPORT bool has_parent_path(std::string const& f);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string filename(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string combine_path(string_view lhs
		, string_view rhs);
	TORRENT_EXTRA_EXPORT void append_path(std::string& branch
		, string_view leaf);
	TORRENT_EXTRA_EXPORT std::string lexically_relative(string_view base
		, string_view target);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string complete(string_view f);
	TORRENT_EXTRA_EXPORT bool is_complete(string_view f);
	TORRENT_EXTRA_EXPORT std::string current_working_directory();
#if TORRENT_USE_UNC_PATHS
	TORRENT_EXTRA_EXPORT std::string canonicalize_path(string_view f);
#endif

// internal type alias export should be used at unit tests only
	using native_path_string =
#if defined TORRENT_WINDOWS
		std::wstring;
#else
		std::string;
#endif

// internal export should be used at unit tests only
	TORRENT_EXTRA_EXPORT native_path_string convert_to_native_path_string(std::string const& path);

#if defined TORRENT_WINDOWS
// internal export should be used at unit tests only
	TORRENT_EXTRA_EXPORT std::string convert_from_native_path(wchar_t const* s);
#else
// internal export should be used at unit tests only
	TORRENT_EXTRA_EXPORT std::string convert_from_native_path(char const* s);
#endif

	TORRENT_EXTRA_EXPORT int bufs_size(span<iovec_t const> bufs);
}

#endif // TORRENT_PATH_HPP_INCLUDED
