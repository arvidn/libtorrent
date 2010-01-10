/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/pch.hpp"
#include "libtorrent/config.hpp"

#include <boost/scoped_ptr.hpp>
#ifdef TORRENT_WINDOWS
// windows part
#include "libtorrent/utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#else
// posix part
#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <boost/static_assert.hpp>
// make sure the _FILE_OFFSET_BITS define worked
// on this platform
BOOST_STATIC_ASSERT(sizeof(lseek(0, 0, 0)) >= 8);

#endif

#include <boost/filesystem/operations.hpp>
#include "libtorrent/file.hpp"
#include <sstream>
#include <cstring>
#include <vector>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_RANDOM
#define O_RANDOM 0
#endif

#if TORRENT_USE_WPATH
// for safe_convert
#include "libtorrent/storage.hpp"
#endif

#include "libtorrent/assert.hpp"

namespace
{
#ifdef TORRENT_WINDOWS
	std::string utf8_native(std::string const& s)
	{
		try
		{
			std::wstring ws;
			libtorrent::utf8_wchar(s, ws);
			std::size_t size = wcstombs(0, ws.c_str(), 0);
			if (size == std::size_t(-1)) return s;
			std::string ret;
			ret.resize(size);
			size = wcstombs(&ret[0], ws.c_str(), size + 1);
			if (size == std::size_t(-1)) return s;
			ret.resize(size);
			return ret;
		}
		catch(std::exception)
		{
			return s;
		}
	}
#else

	enum { mode_in = 1, mode_out = 2 };

	mode_t map_open_mode(int m)
	{
		if (m == (mode_in | mode_out)) return O_RDWR | O_CREAT | O_BINARY | O_RANDOM;
		if (m == mode_out) return O_WRONLY | O_CREAT | O_BINARY | O_RANDOM;
		if (m == mode_in) return O_RDONLY | O_BINARY | O_RANDOM;
		TORRENT_ASSERT(false);
		return 0;
	}
#endif

}

namespace libtorrent
{
	namespace fs = boost::filesystem;

#ifdef TORRENT_WINDOWS
	const file::open_mode file::in(GENERIC_READ);
	const file::open_mode file::out(GENERIC_WRITE);
	const file::seek_mode file::begin(FILE_BEGIN);
	const file::seek_mode file::end(FILE_END);
#else
	const file::open_mode file::in(mode_in);
	const file::open_mode file::out(mode_out);
	const file::seek_mode file::begin(SEEK_SET);
	const file::seek_mode file::end(SEEK_END);
#endif

	file::file()
#ifdef TORRENT_WINDOWS
		: m_file_handle(INVALID_HANDLE_VALUE)
#else
		: m_fd(-1)
#endif
#ifdef TORRENT_DEBUG
		, m_open_mode(0)
#endif
	{}

	file::file(fs::path const& path, open_mode mode, error_code& ec)
#ifdef TORRENT_WINDOWS
		: m_file_handle(INVALID_HANDLE_VALUE)
#else
		: m_fd(-1)
#endif
#ifdef TORRENT_DEBUG
		, m_open_mode(0)
#endif
	{
		open(path, mode, ec);
	}

	file::~file()
	{
		close();
	}

	bool file::open(fs::path const& path, open_mode mode, error_code& ec)
	{
		close();
#ifdef TORRENT_WINDOWS

#if TORRENT_USE_WPATH
		std::wstring file_path(safe_convert(path.external_file_string()));
#else
		std::string file_path = utf8_native(path.external_file_string());
#endif

		m_file_handle = CreateFile(
			file_path.c_str()
			, mode.m_mask
			, FILE_SHARE_READ
			, 0
			, (mode & out)?OPEN_ALWAYS:OPEN_EXISTING
			, FILE_ATTRIBUTE_NORMAL
			, 0);

		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return false;
		}

		// try to make the file sparse if supported
		if (mode & out)
		{
			DWORD temp;
			::DeviceIoControl(m_file_handle, FSCTL_SET_SPARSE, 0, 0
				, 0, 0, &temp, 0);
		}
#else
		// rely on default umask to filter x and w permissions
		// for group and others
		int permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		m_fd = ::open(path.external_file_string().c_str()
			, map_open_mode(mode.m_mask), permissions);

		if (m_fd == -1)
		{
			ec = error_code(errno, get_posix_category());
			return false;
		}
#endif
#ifdef TORRENT_DEBUG
		m_open_mode = mode;
#endif
		TORRENT_ASSERT(is_open());
		return true;
	}

	bool file::is_open() const
	{
#ifdef TORRENT_WINDOWS
		return m_file_handle != INVALID_HANDLE_VALUE;
#else
		return m_fd != -1;
#endif
	}

	void file::close()
	{
#ifdef TORRENT_WINDOWS
		if (m_file_handle == INVALID_HANDLE_VALUE) return;
		CloseHandle(m_file_handle);
		m_file_handle = INVALID_HANDLE_VALUE;
#else
		if (m_fd == -1) return;
		::close(m_fd);
		m_fd = -1;
#endif
#ifdef TORRENT_DEBUG
		m_open_mode = 0;
#endif
	}

	size_type file::read(char* buf, size_type num_bytes, error_code& ec)
	{
		TORRENT_ASSERT((m_open_mode & in) == in);
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(num_bytes >= 0);
		TORRENT_ASSERT(is_open());

#ifdef TORRENT_WINDOWS

		TORRENT_ASSERT(DWORD(num_bytes) == num_bytes);
		DWORD ret = 0;
		if (num_bytes != 0)
		{
			if (ReadFile(m_file_handle, buf, (DWORD)num_bytes, &ret, 0) == FALSE)
			{
				ec = error_code(GetLastError(), get_system_category());
				return -1;
			}
		}
#else
		size_type ret = ::read(m_fd, buf, num_bytes);
		if (ret == -1) ec = error_code(errno, get_posix_category());
#endif
		return ret;
	}

	size_type file::write(const char* buf, size_type num_bytes, error_code& ec)
	{
		TORRENT_ASSERT((m_open_mode & out) == out);
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(num_bytes >= 0);
		TORRENT_ASSERT(is_open());

#ifdef TORRENT_WINDOWS

		DWORD ret = 0;
		if (num_bytes != 0)
		{
			if (WriteFile(m_file_handle, buf, (DWORD)num_bytes, &ret, 0) == FALSE)
			{
				ec = error_code(GetLastError(), get_system_category());
				return -1;
			}
		}
#else
		size_type ret = ::write(m_fd, buf, num_bytes);
		if (ret == -1) ec = error_code(errno, get_posix_category());
#endif
		return ret;
	}

  	bool file::set_size(size_type s, error_code& ec)
  	{
  		TORRENT_ASSERT(is_open());
  		TORRENT_ASSERT(s >= 0);

#ifdef TORRENT_WINDOWS
		size_type pos = tell(ec);
		if (ec) return false;
		seek(s, begin, ec);
		if (ec) return false;
		if (::SetEndOfFile(m_file_handle) == FALSE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return false;
		}
#else
		if (ftruncate(m_fd, s) < 0)
		{
			ec = error_code(errno, get_posix_category());
			return false;
		}
#endif
		return true;
	}

	size_type file::seek(size_type offset, seek_mode m, error_code& ec)
	{
		TORRENT_ASSERT(is_open());

#ifdef TORRENT_WINDOWS
		LARGE_INTEGER offs;
		offs.QuadPart = offset;
		if (SetFilePointerEx(m_file_handle, offs, &offs, m.m_val) == FALSE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return -1;
		}
		return offs.QuadPart;
#else
		size_type ret = lseek(m_fd, offset, m.m_val);
		if (ret < 0) ec = error_code(errno, get_posix_category());
		return ret;
#endif
	}

	size_type file::tell(error_code& ec)
	{
		TORRENT_ASSERT(is_open());

#ifdef TORRENT_WINDOWS
		LARGE_INTEGER offs;
		offs.QuadPart = 0;

		// is there any other way to get offset?
		if (SetFilePointerEx(m_file_handle, offs, &offs
			, FILE_CURRENT) == FALSE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return -1;
		}

		return offs.QuadPart;
#else
		size_type ret;
		ret = lseek(m_fd, 0, SEEK_CUR);
		if (ret < 0) ec = error_code(errno, get_posix_category());
		return ret;
#endif
	}
}

