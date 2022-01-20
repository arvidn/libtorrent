/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
Copyright (c) 2019, Steven Siloti
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

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

#include "libtorrent/aux_/mmap.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/scope_exit.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdint>

#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/win_util.hpp"
#endif

#if TORRENT_HAVE_MMAP
#include <sys/mman.h> // for mmap
#include <sys/stat.h>
#include <fcntl.h> // for open

#include "libtorrent/aux_/disable_warnings_push.hpp"
auto const map_failed = MAP_FAILED;
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace libtorrent {
namespace aux {

namespace {
	std::int64_t memory_map_size(open_mode_t const mode
		, std::int64_t const file_size, file_handle const& fh)
	{
		// if we're opening the file in write-mode, we'll always truncate it to
		// the right size, but in read mode, we should not map more than the
		// file size
		return (mode & open_mode::write)
			? file_size : std::min(std::int64_t(fh.get_size()), file_size);
	}

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

		BOOST_SCOPE_EXIT_ALL(&token) {
			CloseHandle(token);
		};

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

#endif // TORRENT_WINDOWS

} // anonymous

#if TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace {

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
		return CreateFileW(name.c_str()
			, file_access(mode)
			, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE
			, nullptr
			, file_create(mode)
			, file_flags(mode)
			, nullptr);
	}

#endif

} // anonymous

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
		// Enable privilege required by SetFileValidData()
		// https://docs.microsoft.com/en-us/windows/desktop/api/fileapi/nf-fileapi-setfilevaliddata
		std::call_once(g_once_flag, acquire_manage_volume_privs);

		// if the user has permissions, avoid filling
		// the file with zeroes, but just fill it with
		// garbage instead
		SetFileValidData(m_fd, size);
#endif
	}
}

#else

namespace {

	int file_flags(open_mode_t const mode)
	{
		return ((mode & open_mode::write)
			? O_RDWR | O_CREAT : O_RDONLY)
#ifdef O_NOATIME
			| ((mode & open_mode::no_atime) ? O_NOATIME : 0)
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

	int mmap_prot(open_mode_t const m)
	{
		return (m & open_mode::write)
			? (PROT_READ | PROT_WRITE)
			: PROT_READ;
	}

	int mmap_flags(open_mode_t const m)
	{
		TORRENT_UNUSED(m);
		return
			MAP_FILE | MAP_SHARED
#ifdef MAP_NOCACHE
			| ((m & open_mode::no_cache) ? MAP_NOCACHE : 0)
#endif
#ifdef MAP_NOCORE
			// BSD has a flag to exclude this region from core files
			| MAP_NOCORE
#endif
			;
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

} // anonymous

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
			if (ret != 0 && ret != EINVAL)
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
				::close(m_fd);
				throw_ex<storage_error>(error_code(ret, system_category()), operation_t::file_fallocate);
			}
#elif defined F_PREALLOCATE
			fstore_t f = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, size, 0};
			if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
			{
				// It appears Apple's new filesystem (APFS) does not
				// support this control message and fails with EINVAL
				// if so, just skip it
				if (errno != EINVAL)
				{
					if (errno != ENOSPC)
					{
						int const err = errno;
						::close(m_fd);
						throw_ex<storage_error>(error_code(err, system_category())
							, operation_t::file_fallocate);
					}
					// ok, let's try to allocate non contiguous space then
					f.fst_flags = F_ALLOCATEALL;
					if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
					{
						int const err = errno;
						::close(m_fd);
						throw_ex<storage_error>(error_code(err, system_category())
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

#if TORRENT_HAVE_MMAP
	::close(m_fd);
#else
	CloseHandle(m_fd);
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
#if TORRENT_HAVE_MMAP
	struct ::stat fs;
	if (::fstat(fd(), &fs) != 0)
		throw_ex<storage_error>(error_code(errno, system_category()), operation_t::file_stat);
	return fs.st_size;
#else
	LARGE_INTEGER file_size;
	if (GetFileSizeEx(fd(), &file_size) == 0)
		throw_ex<storage_error>(error_code(GetLastError(), system_category()), operation_t::file_stat);
	return file_size.QuadPart;
#endif
}

#if TORRENT_HAVE_MAP_VIEW_OF_FILE

// ======== file mapping handle ========
namespace {

int map_protect(open_mode_t const m)
{
	return (m & open_mode::write) ? PAGE_READWRITE : PAGE_READONLY;
}

}

file_mapping_handle::file_mapping_handle(file_handle file, open_mode_t const mode
	, std::int64_t const size)
	: m_file(std::move(file))
	, m_mapping(CreateFileMapping(m_file.fd()
		, nullptr
		, map_protect(mode)
		, size >> 32
		, size & 0xffffffff
		, nullptr))
	{
		TORRENT_ASSERT(size >= 0);
		// CreateFileMapping will extend the underlying file to the specified // size.
		// you can't map files of size 0, so we just set it to null. We
		// still need to create the empty file.
		if (size > 0 && m_mapping == nullptr)
			throw_ex<system_error>(error_code(GetLastError(), system_category()));
	}
	file_mapping_handle::~file_mapping_handle() { close(); }

	file_mapping_handle::file_mapping_handle(file_mapping_handle&& fm)
		: m_file(std::move(fm.m_file)), m_mapping(fm.m_mapping)
	{
		fm.m_mapping = INVALID_HANDLE_VALUE;
	}

	file_mapping_handle& file_mapping_handle::operator=(file_mapping_handle&& fm) &
	{
		if (&fm == this) return *this;
		close();
		m_file = std::move(fm.m_file);
		m_mapping = fm.m_mapping;
		fm.m_mapping = INVALID_HANDLE_VALUE;
		return *this;
	}

	void file_mapping_handle::close()
	{
		if (m_mapping == INVALID_HANDLE_VALUE) return;
		CloseHandle(m_mapping);
		m_mapping = INVALID_HANDLE_VALUE;
	}

#endif // HAVE_MAP_VIEW_OF_FILE

// =========== file mapping ============

#if TORRENT_HAVE_MMAP

file_mapping::file_mapping(file_handle file, open_mode_t const mode, std::int64_t const file_size)
	: m_size(memory_map_size(mode, file_size, file))
	, m_file(std::move(file))
	, m_mapping(m_size > 0 ? mmap(nullptr, static_cast<std::size_t>(m_size)
			, mmap_prot(mode), mmap_flags(mode), m_file.fd(), 0)
	: nullptr)
{
	TORRENT_ASSERT(file_size >= 0);
	// you can't create an mmap of size 0, so we just set it to null. We
	// still need to create the empty file.
	if (file_size > 0 && m_mapping == map_failed)
	{
		throw_ex<storage_error>(error_code(errno, system_category()), operation_t::file_mmap);
	}

#if TORRENT_USE_MADVISE
	if (file_size > 0)
	{
		int const advise = ((mode & open_mode::random_access) ? 0 : MADV_SEQUENTIAL)
#ifdef MADV_DONTDUMP
		// on versions of linux that support it, ask for this region to not be
		// included in coredumps (mostly to make the coredumps more manageable
		// with large disk caches)
		// ignore errors here, since this is best-effort
			| MADV_DONTDUMP
#endif
#ifdef MADV_NOCORE
		// This is the BSD counterpart to exclude a range from core dumps
			| MADV_NOCORE
#endif
		;
		if (advise != 0)
			madvise(m_mapping, static_cast<std::size_t>(m_size), advise);
	}
#endif
}

void file_mapping::close()
{
	if (m_mapping == nullptr) return;
	munmap(m_mapping, static_cast<std::size_t>(m_size));
	m_mapping = nullptr;
}

#else

namespace {
DWORD map_access(open_mode_t const m)
{
	return (m & open_mode::write) ? FILE_MAP_READ | FILE_MAP_WRITE : FILE_MAP_READ ;
}
} // anonymous

file_mapping::file_mapping(file_handle file, open_mode_t const mode
	, std::int64_t const file_size
	, std::shared_ptr<std::mutex> open_unmap_lock)
	: m_size(memory_map_size(mode, file_size, file))
	, m_file(std::move(file), mode, m_size)
	, m_open_unmap_lock(open_unmap_lock)
	, m_mapping(MapViewOfFile(m_file.handle()
		, map_access(mode), 0, 0, static_cast<std::size_t>(m_size)))
{
	// you can't create an mmap of size 0, so we just set it to null. We
	// still need to create the empty file.
	if (m_size > 0 && m_mapping == nullptr)
		throw_ex<storage_error>(error_code(GetLastError(), system_category()), operation_t::file_mmap);
}

void file_mapping::flush()
{
	if (m_mapping == nullptr) return;

	// ignore errors, this is best-effort
	FlushViewOfFile(m_mapping, static_cast<std::size_t>(m_size));
}

void file_mapping::close()
{
	if (m_mapping == nullptr) return;
	flush();
	std::lock_guard<std::mutex> l(*m_open_unmap_lock);
	UnmapViewOfFile(m_mapping);
	m_mapping = nullptr;
}
#endif

file_mapping::file_mapping(file_mapping&& rhs)
	: m_size(rhs.m_size)
	, m_file(std::move(rhs.m_file))
	, m_mapping(rhs.m_mapping)
	{
		TORRENT_ASSERT(m_mapping);
		rhs.m_mapping = nullptr;
	}

	file_mapping& file_mapping::operator=(file_mapping&& rhs) &
	{
		if (&rhs == this) return *this;
		close();
		m_file = std::move(rhs.m_file);
		m_size = rhs.m_size;
		m_mapping = rhs.m_mapping;
		rhs.m_mapping = nullptr;
		return *this;
	}

	file_mapping::~file_mapping() { close(); }

	file_view file_mapping::view()
	{
		return file_view(shared_from_this());
	}


void file_mapping::dont_need(span<byte const> range)
{
	TORRENT_UNUSED(range);
#if TORRENT_USE_MADVISE
	int const advise = 0
#if defined TORRENT_LINUX && defined MADV_COLD
		| MADV_COLD
#elif !defined TORRENT_LINUX && defined MADV_DONTNEED
		// note that MADV_DONTNEED is broken on Linux. It can destroy data. We
		// cannot use it
		| MADV_DONTNEED
#endif
	;

	if (advise)
		madvise(const_cast<byte*>(range.data()), static_cast<std::size_t>(range.size()), advise);
#endif
}

} // aux
} // libtorrent

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

