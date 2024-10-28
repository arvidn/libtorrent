/*

Copyright (c) 2004, 2008-2010, 2014-2018, 2020-2021, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

namespace libtorrent::aux {

#ifdef TORRENT_WINDOWS
	using handle_type = HANDLE;
	const handle_type invalid_handle = INVALID_HANDLE_VALUE;
#else
	using handle_type = int;
	const handle_type invalid_handle = -1;
#endif

	int pwrite_all(handle_type handle
		, span<char const> buf
		, std::int64_t file_offset
		, error_code& ec);

	int pwritev_all(handle_type handle
		, span<span<char const> const> bufs
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

		bool has_memory_map() const { return false; }

		handle_type fd() const { return m_fd; }
	private:
		void close();
		handle_type m_fd;
#ifdef TORRENT_WINDOWS
		aux::open_mode_t m_open_mode;
#endif
	};

}

#endif // TORRENT_FILE_HPP_INCLUDED
