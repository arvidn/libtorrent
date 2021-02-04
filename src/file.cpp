/*

Copyright (c) 2018, d-komarov
Copyright (c) 2004-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2016-2017, Steven Siloti
Copyright (c) 2016-2017, Andrei Kurushin
Copyright (c) 2020, Tiger Wang
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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include "libtorrent/span.hpp"
#include <mutex> // for call_once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-macros"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-macros"
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif

#ifndef TORRENT_WINDOWS
#include <sys/uio.h> // for iovec
#else
#include <boost/scope_exit.hpp>
namespace {
struct iovec
{
	void* iov_base;
	std::size_t iov_len;
};
} // anonymous namespace
#endif

// on mingw this is necessary to enable 64-bit time_t, specifically used for
// the stat struct. Without this, modification times returned by stat may be
// incorrect and consistently fail resume data
#ifndef __MINGW_USE_VC2005_COMPAT
# define __MINGW_USE_VC2005_COMPAT
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/file.hpp"
#include "libtorrent/aux_/path.hpp" // for convert_to_native_path_string
#include "libtorrent/string_util.hpp"
#include <cstring>

#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/open_mode.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <sys/stat.h>

#ifdef TORRENT_WINDOWS
// windows part

#include "libtorrent/aux_/win_util.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <sys/types.h>
#else
// posix part

#include <unistd.h>
#include <sys/types.h>
#include <cerrno>
#include <dirent.h>

#ifdef TORRENT_LINUX
// linux specifics

#include <sys/ioctl.h>
#ifdef TORRENT_ANDROID
#include <sys/syscall.h>
#endif

#endif

#endif // posix part

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (-1)
#endif

namespace libtorrent {

#ifdef TORRENT_WINDOWS
namespace {
	std::int64_t pread(HANDLE fd, void* data, std::size_t len, std::int64_t const offset)
	{
		OVERLAPPED ol{};
		ol.Offset = offset & 0xffffffff;
		ol.OffsetHigh = offset >> 32;
		DWORD bytes_read = 0;
		if (ReadFile(fd, data, DWORD(len), &bytes_read, &ol) == FALSE)
		{
			if (GetLastError() == ERROR_HANDLE_EOF) return 0;
			return -1;
		}

		return bytes_read;
	}

	std::int64_t pwrite(HANDLE fd, void const* data, std::size_t len, std::int64_t const offset)
	{
		OVERLAPPED ol{};
		ol.Offset = offset & 0xffffffff;
		ol.OffsetHigh = offset >> 32;
		DWORD bytes_written = 0;
		if (WriteFile(fd, data, DWORD(len), &bytes_written, &ol) == FALSE)
			return -1;

		return bytes_written;
	}
}
#endif

	file::file() : m_file_handle(INVALID_HANDLE_VALUE) {}

	file::file(file&& f) noexcept
		: m_file_handle(f.m_file_handle)
	{
		f.m_file_handle = INVALID_HANDLE_VALUE;
	}

	file& file::operator=(file&& f)
	{
		file tmp(std::move(*this)); // close at end of scope
		m_file_handle = f.m_file_handle;
		f.m_file_handle = INVALID_HANDLE_VALUE;
		return *this;
	}

	file::~file()
	{
		if (m_file_handle == INVALID_HANDLE_VALUE) return;

#ifdef TORRENT_WINDOWS
		CloseHandle(m_file_handle);
#else
		if (m_file_handle != INVALID_HANDLE_VALUE)
			::close(m_file_handle);
#endif

		m_file_handle = INVALID_HANDLE_VALUE;
	}

	file::file(std::string const& path, aux::open_mode_t const mode, error_code& ec)
		: m_file_handle(INVALID_HANDLE_VALUE)
	{
		// the return value is not important, since the
		// error code contains the same information
		native_path_string file_path = convert_to_native_path_string(path);

#ifdef TORRENT_WINDOWS
#ifdef TORRENT_WINRT

		const auto handle = CreateFile2(file_path.c_str()
			, (mode & aux::open_mode::write) ? GENERIC_WRITE | GENERIC_READ : GENERIC_READ
			, FILE_SHARE_READ | FILE_SHARE_WRITE
			, (mode & aux::open_mode::write) ? OPEN_ALWAYS : OPEN_EXISTING
			, nullptr);

#else

		handle_type handle = CreateFileW(file_path.c_str()
			, (mode & aux::open_mode::write) ? GENERIC_WRITE | GENERIC_READ : GENERIC_READ
			, FILE_SHARE_READ | FILE_SHARE_WRITE
			, nullptr
			, (mode & aux::open_mode::write) ? OPEN_ALWAYS : OPEN_EXISTING
			, 0, nullptr);

#endif

		if (handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), system_category());
			TORRENT_ASSERT(ec);
			return;
		}

		m_file_handle = handle;
#else // TORRENT_WINDOWS

		handle_type handle = ::open(file_path.c_str()
			, ((mode & aux::open_mode::write) ? O_RDWR | O_CREAT : O_RDONLY)
#ifdef O_BINARY
			| O_BINARY
#endif
			, S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH);

		if (handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(errno, system_category());
			TORRENT_ASSERT(ec);
			return;
		}

		m_file_handle = handle;
#endif

		TORRENT_ASSERT(m_file_handle != INVALID_HANDLE_VALUE);
	}

namespace {

	template <class Fun>
	std::int64_t iov(Fun f, handle_type fd, std::int64_t file_offset
		, span<iovec_t const> bufs, error_code& ec)
	{
		std::int64_t ret = 0;
		for (auto i : bufs)
		{
			std::int64_t const tmp_ret = f(fd, i.data(), static_cast<std::size_t>(i.size()), file_offset);
			if (tmp_ret < 0)
			{
#ifdef TORRENT_WINDOWS
				ec.assign(GetLastError(), system_category());
#else
				ec.assign(errno, system_category());
#endif
				return -1;
			}
			file_offset += tmp_ret;
			ret += tmp_ret;
			if (tmp_ret < int(i.size())) break;
		}

		return ret;
	}

} // anonymous namespace

	// this has to be thread safe and atomic. i.e. on posix systems it has to be
	// turned into a series of pread() calls
	std::int64_t file::readv(std::int64_t file_offset, span<iovec_t const> bufs
		, error_code& ec, aux::open_mode_t)
	{
		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
#ifdef TORRENT_WINDOWS
			ec = error_code(ERROR_INVALID_HANDLE, system_category());
#else
			ec = error_code(boost::system::errc::bad_file_descriptor, generic_category());
#endif
			return -1;
		}
		TORRENT_ASSERT(!bufs.empty());
		TORRENT_ASSERT(m_file_handle != INVALID_HANDLE_VALUE);

		return iov(&pread, m_file_handle, file_offset, bufs, ec);
	}

	// This has to be thread safe, i.e. atomic.
	// that means, on posix this has to be turned into a series of
	// pwrite() calls
	std::int64_t file::writev(std::int64_t file_offset, span<iovec_t const> bufs
		, error_code& ec, aux::open_mode_t)
	{
		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
#ifdef TORRENT_WINDOWS
			ec = error_code(ERROR_INVALID_HANDLE, system_category());
#else
			ec = error_code(boost::system::errc::bad_file_descriptor, generic_category());
#endif
			return -1;
		}
		TORRENT_ASSERT(!bufs.empty());
		TORRENT_ASSERT(m_file_handle != INVALID_HANDLE_VALUE);

		ec.clear();

		std::int64_t ret = iov(&pwrite, m_file_handle, file_offset, bufs, ec);

		return ret;
	}
}
