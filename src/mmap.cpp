/*

Copyright (c) 2017, Arvid Norberg
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
#include <cstdint>

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
} // anonymous

#if TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace {

	DWORD file_access(open_mode_t const mode)
	{
		return (mode & open_mode::write)
			? GENERIC_WRITE | GENERIC_READ
			: GENERIC_READ;
	}

	DWORD file_share(open_mode_t const mode)
	{
		return (mode & open_mode::lock_files)
			? FILE_SHARE_READ
			: FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE;
	}

	DWORD file_create(open_mode_t const mode)
	{
		return (mode & open_mode::write) ? OPEN_ALWAYS : OPEN_EXISTING;
	}

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

} // anonymous

file_handle::file_handle(string_view name, std::int64_t
	, open_mode_t const mode)
	: m_fd(CreateFileW(convert_to_native_path_string(name.to_string()).c_str()
		, file_access(mode)
		, file_share(mode)
		, nullptr
		, file_create(mode)
		, file_flags(mode)
		, nullptr))
		, m_open_mode(mode)
{
	if (m_fd == invalid_handle) throw_ex<system_error>(error_code(GetLastError(), system_category()));

	// try to make the file sparse if supported
	// only set this flag if the file is opened for writing
	if ((mode & aux::open_mode::sparse)
		&& (mode & aux::open_mode::write))
	{
		DWORD temp;
		::DeviceIoControl(m_fd, FSCTL_SET_SPARSE, 0, 0, 0, 0, &temp, nullptr);
	}
}

#else

namespace {

	int file_flags(open_mode_t const mode)
	{
		return ((mode & open_mode::write)
			? O_RDWR | O_CREAT : O_RDONLY)
#ifdef O_NOATIME
			| ((mode & open_mode::no_atime)
			? O_NOATIME : 0)
#endif
			;
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
			| ((m & open_mode::no_cache)
			? MAP_NOCACHE
			: 0)
#endif
#ifdef MAP_NOCORE
			// BSD has a flag to exclude this region from core files
			| MAP_NOCORE
#endif
			;
	}
} // anonymous

file_handle::file_handle(string_view name, std::int64_t const size
	, open_mode_t const mode)
	: m_fd(open(name.to_string().c_str(), file_flags(mode), 0755))
{
#ifdef O_NOATIME
	if (m_fd < 0 && (mode & open_mode::no_atime))
	{
		// NOATIME may not be allowed for certain files, it's best-effort,
		// so just try again without NOATIME
		m_fd = open(name.to_string().c_str()
			, file_flags(mode & ~open_mode::no_atime), 0755);
	}
#endif
	if (m_fd < 0) throw_ex<system_error>(error_code(errno, system_category()));

	// The purpose of the lock_file flag is primarily to prevent other
	// processes from corrupting files that are being used by libtorrent.
	// the posix file locking mechanism does not prevent others from
	// accessing files, unless they also attempt to lock the file. That's
	// why the SETLK mechanism is not used here.

	if (mode & open_mode::truncate)
	{
		if (ftruncate(m_fd, static_cast<off_t>(size)) < 0)
		{
			int const err = errno;
			::close(m_fd);
			throw_ex<system_error>(error_code(err, system_category()));
		}

		if (!(mode & open_mode::sparse))
		{
#if TORRENT_HAS_FALLOCATE
			// if fallocate failed, we have to use posix_fallocate
			// which can be painfully slow
			// if you get a compile error here, you might want to
			// define TORRENT_HAS_FALLOCATE to 0.
			int const ret = posix_fallocate(m_fd, 0, size);
			// posix_allocate fails with EINVAL in case the underlying
			// filesystem does not support this operation
			if (ret != 0 && ret != EINVAL)
			{
				::close(m_fd);
				throw_ex<system_error>(error_code(ret, system_category()));
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
				throw_ex<system_error>(error_code(err, system_category()));
			}
#elif defined F_PREALLOCATE
			fstore_t f = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, size, 0};
			if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
			{
				if (errno != ENOSPC)
				{
					int const err = errno;
					::close(m_fd);
					throw_ex<system_error>(error_code(err, system_category()));
				}
				// ok, let's try to allocate non contiguous space then
				f.fst_flags = F_ALLOCATEALL;
				if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
				{
					int const err = errno;
					::close(m_fd);
					throw_ex<system_error>(error_code(err, system_category()));
				}
			}
#endif // F_PREALLOCATE
		}
	}
}
#endif

#ifdef TORRENT_WINDOWS
namespace {
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
		BOOL ret = DeviceIoControl(file, FSCTL_QUERY_ALLOCATED_RANGES, (void*)&in, sizeof(in)
			, out, sizeof(out), &returned_bytes, nullptr);

		if (ret == FALSE) return true;

		// if we have more than one range in the file, we're sparse
		if (returned_bytes != sizeof(FILE_ALLOCATED_RANGE_BUFFER)) {
			return true;
		}

		return (in.Length.QuadPart != out[0].Length.QuadPart);
	}
} // anonymous namespace
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
			, 0, 0, &temp, nullptr);
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

file_handle& file_handle::operator=(file_handle&& rhs)
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
		throw_ex<system_error>(error_code(errno, system_category()));
	return fs.st_size;
#else
	LARGE_INTEGER file_size;
	if (GetFileSizeEx(fd(), &file_size) == 0)
		throw_ex<system_error>(error_code(GetLastError(), system_category()));
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

	file_mapping_handle& file_mapping_handle::operator=(file_mapping_handle&& fm)
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
		throw_ex<system_error>(error_code(errno, system_category()));
	}

#if TORRENT_USE_MADVISE && defined MADV_DONTDUMP
	if (file_size > 0)
	{
		// on versions of linux that support it, ask for this region to not be
		// included in coredumps (mostly to make the coredumps more manageable
		// with large disk caches)
		// ignore errors here, since this is best-effort
		madvise(m_mapping, static_cast<std::size_t>(m_size), MADV_DONTDUMP);
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
	, std::int64_t const file_size)
	: m_size(memory_map_size(mode, file_size, file))
	, m_file(std::move(file), mode, m_size)
	, m_mapping(MapViewOfFile(m_file.handle()
		, map_access(mode), 0, 0, static_cast<std::size_t>(m_size)))
{
	// you can't create an mmap of size 0, so we just set it to null. We
	// still need to create the empty file.
	if (file_size > 0 && m_mapping == nullptr)
	{
		fprintf(stderr, "MapViewOfFile failed: %d\n", GetLastError());
		throw_ex<system_error>(error_code(GetLastError(), system_category()));
	}
}

void file_mapping::close()
{
	if (m_mapping == nullptr) return;
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

	file_mapping& file_mapping::operator=(file_mapping&& rhs)
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

} // aux
} // libtorrent

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

