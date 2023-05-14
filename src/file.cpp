/*

Copyright (c) 2018, d-komarov
Copyright (c) 2004-2005, 2007-2020, 2022, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2016-2017, Andrei Kurushin
Copyright (c) 2016-2017, Steven Siloti
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

#include "libtorrent/file.hpp"
#include "libtorrent/aux_/path.hpp" // for convert_to_native_path_string
#include "libtorrent/string_util.hpp"
#include <cstring>

#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <sys/stat.h>

#ifdef TORRENT_WINDOWS
// windows part

#include "libtorrent/aux_/win_util.hpp"
#include "libtorrent/aux_/scope_end.hpp"

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

#include <boost/asio/error.hpp> // for boost::asio::error::eof

#ifdef TORRENT_LINUX
// linux specifics

#include <sys/ioctl.h>
#ifdef TORRENT_ANDROID
#include <sys/syscall.h>
#endif

#endif

#endif // posix part

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {
namespace aux {

#ifdef TORRENT_WINDOWS
	int pread_all(handle_type const fd
		, span<char> const buf
		, std::int64_t const offset
		, error_code& ec)
	{
		OVERLAPPED ol{};
		ol.Offset = offset & 0xffffffff;
		ol.OffsetHigh = offset >> 32;
		DWORD bytes_read = 0;
		if (ReadFile(fd, buf.data(), DWORD(buf.size()), &bytes_read, &ol) == FALSE)
		{
			ec = error_code(::GetLastError(), system_category());
			return -1;
		}

		return int(bytes_read);
	}

	int pwrite_all(handle_type const fd
		, span<char const> const buf
		, std::int64_t const offset
		, error_code& ec)
	{
		OVERLAPPED ol{};
		ol.Offset = offset & 0xffffffff;
		ol.OffsetHigh = offset >> 32;
		DWORD bytes_written = 0;
		if (WriteFile(fd, buf.data(), DWORD(buf.size()), &bytes_written, &ol) == FALSE)
		{
			ec = error_code(::GetLastError(), system_category());
			return -1;
		}

		return int(bytes_written);
	}
#else

	int pread_all(handle_type const handle
		, span<char> buf
		, std::int64_t file_offset
		, error_code& ec)
	{
		int ret = 0;
		do {
			auto const r = ::pread(handle, buf.data(), std::size_t(buf.size()), file_offset);
			if (r == 0)
			{
				ec = boost::asio::error::eof;
				return ret;
			}
			if (r < 0)
			{
				ec = error_code(errno, system_category());
				return ret;
			}
			ret += r;
			file_offset += r;
			buf = buf.subspan(r);
		} while (buf.size() > 0);
		return ret;
	}

	int pwrite_all(handle_type const handle
		, span<char const> buf
		, std::int64_t file_offset
		, error_code& ec)
	{
		int ret = 0;
		do {
			auto const r = ::pwrite(handle, buf.data(), std::size_t(buf.size()), file_offset);
			if (r == 0)
			{
				ec = boost::asio::error::eof;
				return ret;
			}
			if (r < 0)
			{
				ec = error_code(errno, system_category());
				return -1;
			}
			ret += r;
			file_offset += r;
			buf = buf.subspan(r);
		} while (buf.size() > 0);
		return ret;
	}
#endif

namespace {
#ifdef TORRENT_WINDOWS
	// returns true if the given file has any regions that are
	// sparse, i.e. not allocated.
	bool is_sparse(HANDLE file)
	{
		LARGE_INTEGER file_size;
		if (!GetFileSizeEx(file, &file_size))
			return false;

#ifndef FSCTL_QUERY_ALLOCATED_RANGES
typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
	LARGE_INTEGER FileOffset;
	LARGE_INTEGER Length;
} FILE_ALLOCATED_RANGE_BUFFER;
#define FSCTL_QUERY_ALLOCATED_RANGES ((0x9 << 16) | (1 << 14) | (51 << 2) | 3)
#endif
		FILE_ALLOCATED_RANGE_BUFFER in;
		in.FileOffset.QuadPart = 0;
		in.Length.QuadPart = file_size.QuadPart;

		FILE_ALLOCATED_RANGE_BUFFER out[2];

		DWORD returned_bytes = 0;
		BOOL ret = DeviceIoControl(file, FSCTL_QUERY_ALLOCATED_RANGES, static_cast<void*>(&in), sizeof(in)
			, out, sizeof(out), &returned_bytes, nullptr);

		if (ret == FALSE)
		{
			return true;
		}

		// if we have more than one range in the file, we're sparse
		if (returned_bytes != sizeof(FILE_ALLOCATED_RANGE_BUFFER)) {
			return true;
		}

		return (in.Length.QuadPart != out[0].Length.QuadPart);
	}

	DWORD file_access(open_mode_t const mode)
	{
		return (mode & open_mode::write)
			? GENERIC_WRITE | GENERIC_READ
			: GENERIC_READ;
	}

	DWORD file_create(open_mode_t const mode)
	{
		return (mode & open_mode::write) ? OPEN_ALWAYS : OPEN_EXISTING;
	}


#ifndef TORRENT_WINRT
	std::once_flag g_once_flag;

	void acquire_manage_volume_privs()
	{
		using OpenProcessToken_t = BOOL (WINAPI*)(HANDLE, DWORD, PHANDLE);

		using LookupPrivilegeValue_t = BOOL (WINAPI*)(LPCSTR, LPCSTR, PLUID);

		using AdjustTokenPrivileges_t = BOOL (WINAPI*)(
			HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);

		auto OpenProcessToken =
			aux::get_library_procedure<aux::advapi32, OpenProcessToken_t>("OpenProcessToken");
		auto LookupPrivilegeValue =
			aux::get_library_procedure<aux::advapi32, LookupPrivilegeValue_t>("LookupPrivilegeValueA");
		auto AdjustTokenPrivileges =
			aux::get_library_procedure<aux::advapi32, AdjustTokenPrivileges_t>("AdjustTokenPrivileges");

		if (OpenProcessToken == nullptr
			|| LookupPrivilegeValue == nullptr
			|| AdjustTokenPrivileges == nullptr)
		{
			return;
		}

		HANDLE token;
		if (!OpenProcessToken(GetCurrentProcess()
			, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
			return;

		auto close_handle = aux::scope_end([&token] {
			CloseHandle(token);
		});

		TOKEN_PRIVILEGES privs{};
		if (!LookupPrivilegeValue(nullptr, "SeManageVolumePrivilege"
			, &privs.Privileges[0].Luid))
		{
			return;
		}

		privs.PrivilegeCount = 1;
		privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		AdjustTokenPrivileges(token, FALSE, &privs, 0, nullptr, nullptr);
	}
#endif // TORRENT_WINRT

#ifdef TORRENT_WINRT

	DWORD file_flags(open_mode_t const mode)
	{
		return ((mode & open_mode::no_cache) ? FILE_FLAG_WRITE_THROUGH : 0)
			| ((mode & open_mode::random_access) ? 0 : FILE_FLAG_SEQUENTIAL_SCAN)
			;
	}

	DWORD file_attributes(open_mode_t const mode)
	{
		return (mode & open_mode::hidden) ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL;
	}

	auto create_file(const native_path_string & name, open_mode_t const mode)
	{
		CREATEFILE2_EXTENDED_PARAMETERS Extended
		{
			sizeof(CREATEFILE2_EXTENDED_PARAMETERS),
			file_attributes(mode),
			file_flags(mode)
		};

		return CreateFile2(name.c_str()
			, file_access(mode)
			, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE
			, file_create(mode)
			, &Extended);
	}

#else

	DWORD file_flags(open_mode_t const mode)
	{
		// one might think it's a good idea to pass in FILE_FLAG_RANDOM_ACCESS. It
		// turns out that it isn't. That flag will break your operating system:
		// http://support.microsoft.com/kb/2549369
		return ((mode & open_mode::hidden) ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL)
			| ((mode & open_mode::no_cache) ? FILE_FLAG_WRITE_THROUGH : 0)
			| ((mode & open_mode::random_access) ? 0 : FILE_FLAG_SEQUENTIAL_SCAN)
			;
	}

	auto create_file(const native_path_string & name, open_mode_t const mode)
	{

		if (mode & aux::open_mode::allow_set_file_valid_data)
		{
			// Enable privilege required by SetFileValidData()
			// https://docs.microsoft.com/en-us/windows/desktop/api/fileapi/nf-fileapi-setfilevaliddata
			// This must happen before the file is opened:
			// https://devblogs.microsoft.com/oldnewthing/20160603-00/?p=93565
			// SetFileValidData() is not available on WinRT, so there is no
			// corresponding call in that version of create_file()
			std::call_once(g_once_flag, acquire_manage_volume_privs);
		}

		return CreateFileW(name.c_str()
			, file_access(mode)
			, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE
			, nullptr
			, file_create(mode)
			, file_flags(mode)
			, nullptr);
	}

#endif

#else
	// non-windows

	int file_flags(open_mode_t const mode)
	{
		return ((mode & open_mode::write)
			? O_RDWR | O_CREAT : O_RDONLY)
#ifdef O_NOATIME
			| ((mode & open_mode::no_atime) ? O_NOATIME : 0)
#endif
#ifdef O_SYNC
			| ((mode & open_mode::no_cache) ? O_SYNC : 0)
#endif
			;
	}

	mode_t file_perms(open_mode_t const mode)
	{
		// rely on default umask to filter x and w permissions
		// for group and others
		mode_t permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		if ((mode & aux::open_mode::executable))
			permissions |= S_IXGRP | S_IXOTH | S_IXUSR;

		return permissions;
	}

	int open_file(std::string const filename, open_mode_t const mode)
	{
		int ret = ::open(filename.c_str(), file_flags(mode), file_perms(mode));

#ifdef O_NOATIME
		if (ret < 0 && (mode & open_mode::no_atime))
		{
			// NOATIME may not be allowed for certain files, it's best-effort,
			// so just try again without NOATIME
			ret = ::open(filename.c_str()
				, file_flags(mode & ~open_mode::no_atime), file_perms(mode));
		}
#endif
		if (ret < 0) throw_ex<storage_error>(error_code(errno, system_category()), operation_t::file_open);
		return ret;
	}
#endif // TORRENT_WINDOWS

} // anonymous namespace

#ifdef TORRENT_WINDOWS
file_handle::file_handle(string_view name, std::int64_t const size
	, open_mode_t const mode)
	: m_fd(create_file(convert_to_native_path_string(name.to_string()), mode))
	, m_open_mode(mode)
{
	if (m_fd == invalid_handle)
	{
		throw_ex<storage_error>(error_code(GetLastError(), system_category())
			, operation_t::file_open);
	}

	// try to make the file sparse if supported
	// only set this flag if the file is opened for writing
	if ((mode & aux::open_mode::sparse) && (mode & aux::open_mode::write))
	{
		DWORD temp;
		::DeviceIoControl(m_fd, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &temp, nullptr);
	}

	if ((mode & open_mode::truncate)
		&& !(mode & aux::open_mode::sparse)
		&& (mode & aux::open_mode::allow_set_file_valid_data))
	{
		LARGE_INTEGER sz;
		sz.QuadPart = size;
		if (SetFilePointerEx(m_fd, sz, nullptr, FILE_BEGIN) == FALSE)
			throw_ex<storage_error>(error_code(GetLastError(), system_category()), operation_t::file_seek);

		if (::SetEndOfFile(m_fd) == FALSE)
			throw_ex<storage_error>(error_code(GetLastError(), system_category()), operation_t::file_truncate);

#ifndef TORRENT_WINRT
		// if the user has permissions, avoid filling
		// the file with zeroes, but just fill it with
		// garbage instead
		SetFileValidData(m_fd, size);
#endif
	}
}

#else

file_handle::file_handle(string_view name, std::int64_t const size
	, open_mode_t const mode)
	: m_fd(open_file(convert_to_native_path_string(name.to_string()), mode))
{
#ifdef DIRECTIO_ON
	// for solaris
	if (mode & open_mode::no_cache)
		directio(m_fd, DIRECTIO_ON);
#endif

	if (mode & open_mode::truncate)
	{
		static_assert(sizeof(off_t) >= sizeof(size), "There seems to be a large-file issue in truncate()");
		if (ftruncate(m_fd, static_cast<off_t>(size)) < 0)
		{
			int const err = errno;
			::close(m_fd);
			throw_ex<storage_error>(error_code(err, system_category()), operation_t::file_truncate);
		}

		if (!(mode & open_mode::sparse))
		{
#if TORRENT_HAS_FALLOCATE
			// if you get a compile error here, you might want to
			// define TORRENT_HAS_FALLOCATE to 0.
			int const ret = ::posix_fallocate(m_fd, 0, size);
			// posix_allocate fails with EINVAL in case the underlying
			// filesystem does not support this operation
			if (ret != 0 && ret != EINVAL && ret != ENOTSUP)
			{
				::close(m_fd);
				throw_ex<storage_error>(error_code(ret, system_category()), operation_t::file_fallocate);
			}
#elif defined F_ALLOCSP64
			flock64 fl64;
			fl64.l_whence = SEEK_SET;
			fl64.l_start = 0;
			fl64.l_len = size;
			if (fcntl(m_fd, F_ALLOCSP64, &fl64) < 0)
			{
				int const err = errno;
				if (err != ENOTSUP)
				{
					::close(m_fd);
					throw_ex<storage_error>(error_code(err, system_category()), operation_t::file_fallocate);
				}
			}
#elif defined F_PREALLOCATE
			fstore_t f = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, size, 0};
			if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
			{
				// It appears Apple's new filesystem (APFS) does not
				// support this control message and fails with EINVAL
				// if so, just skip it
				int const err = errno;
				if (err != EINVAL && err != ENOTSUP)
				{
					if (err != ENOSPC)
					{
						::close(m_fd);
						throw_ex<storage_error>(error_code(err, system_category())
							, operation_t::file_fallocate);
					}
					// ok, let's try to allocate non contiguous space then
					f.fst_flags = F_ALLOCATEALL;
					if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
					{
						int const err2 = errno;
						::close(m_fd);
						throw_ex<storage_error>(error_code(err2, system_category())
							, operation_t::file_fallocate);
					}
				}
			}
#endif // F_PREALLOCATE
		}
	}

#ifdef F_NOCACHE
	// for BSD/Mac
	if (mode & aux::open_mode::no_cache)
	{
		int yes = 1;
		::fcntl(m_fd, F_NOCACHE, &yes);

#ifdef F_NODIRECT
		// it's OK to temporarily cache written pages
		::fcntl(m_fd, F_NODIRECT, &yes);
#endif
	}
#endif

#if (TORRENT_HAS_FADVISE && defined POSIX_FADV_RANDOM)
	if (mode & aux::open_mode::random_access)
	{
		// disable read-ahead
		::posix_fadvise(m_fd, 0, 0, POSIX_FADV_RANDOM);
	}
#endif
}
#endif

void file_handle::close()
{
	if (m_fd == invalid_handle) return;

#ifdef TORRENT_WINDOWS

	// if this file is open for writing, has the sparse
	// flag set, but there are no sparse regions, unset
	// the flag
	if ((m_open_mode & aux::open_mode::write)
		&& (m_open_mode & aux::open_mode::sparse)
		&& !is_sparse(m_fd))
	{
		// according to MSDN, clearing the sparse flag of a file only
		// works on windows vista and later
#ifdef TORRENT_MINGW
		typedef struct _FILE_SET_SPARSE_BUFFER {
			BOOLEAN SetSparse;
		} FILE_SET_SPARSE_BUFFER;
#endif
		DWORD temp;
		FILE_SET_SPARSE_BUFFER b;
		b.SetSparse = FALSE;
		::DeviceIoControl(m_fd, FSCTL_SET_SPARSE, &b, sizeof(b)
			, nullptr, 0, &temp, nullptr);
	}
#endif

#ifdef TORRENT_WINDOWS
	CloseHandle(m_fd);
#else
	::close(m_fd);
#endif
	m_fd = invalid_handle;
}

file_handle::~file_handle() { close(); }

file_handle& file_handle::operator=(file_handle&& rhs) &
{
	if (&rhs == this) return *this;
	close();
	m_fd = rhs.m_fd;
	rhs.m_fd = invalid_handle;
	return *this;
}

std::int64_t file_handle::get_size() const
{
#ifdef  TORRENT_WINDOWS
	LARGE_INTEGER file_size;
	if (GetFileSizeEx(fd(), &file_size) == 0)
		throw_ex<storage_error>(error_code(GetLastError(), system_category()), operation_t::file_stat);
	return file_size.QuadPart;
#else
	struct ::stat fs;
	if (::fstat(fd(), &fs) != 0)
		throw_ex<storage_error>(error_code(errno, system_category()), operation_t::file_stat);
	return fs.st_size;
#endif
}

} // namespace aux

}
