/*

Copyright (c) 2016, 2019-2022, Arvid Norberg
Copyright (c) 2019, Steven Siloti
Copyright (c) 2020, Tiger Wang
Copyright (c) 2021, Alden Torres
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
#include "libtorrent/file.hpp" // for file_handle

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

#if TORRENT_USE_SYNC_FILE_RANGE
#include <fcntl.h> // for sync_file_range
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

#if !TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace {

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

} // anonymous

#endif

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
	, m_mapping((mode & open_mode::no_mmap) ? nullptr
		: mmap(nullptr, static_cast<std::size_t>(m_size)
			, mmap_prot(mode), mmap_flags(mode), m_file.fd(), 0))
{
	TORRENT_ASSERT(file_size >= 0);
	// you can't create an mmap of size 0, so we just set it to null. We
	// still need to create the empty file.
	if (!(mode & open_mode::no_mmap) && m_mapping == map_failed)
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
	, m_mapping((mode & open_mode::no_mmap) ? nullptr
		: MapViewOfFile(m_file.handle(), map_access(mode), 0, 0, static_cast<std::size_t>(m_size)))
{
	// you can't create an mmap of size 0, so we just set it to null. We
	// still need to create the empty file.
	if (!(mode & open_mode::no_mmap) && m_mapping == nullptr)
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

void file_mapping::dont_need(span<byte const> range)
{
	auto* const start = const_cast<byte*>(range.data());
	auto const size = static_cast<std::size_t>(range.size());

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
		::madvise(start, size, advise);
#endif
#ifndef TORRENT_WINDOWS
	::msync(start, size, MS_INVALIDATE);
#else
	TORRENT_UNUSED(start);
	TORRENT_UNUSED(size);
#endif
}

void file_mapping::page_out(span<byte const> range)
{
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
	// ignore errors, this is best-effort
	FlushViewOfFile(range.data(), static_cast<std::size_t>(range.size()));
#else

	auto* const start = const_cast<byte*>(range.data());
	auto const size = static_cast<std::size_t>(range.size());
#if TORRENT_USE_MADVISE && defined MADV_PAGEOUT
	::madvise(start, size, MADV_PAGEOUT);
#elif TORRENT_USE_SYNC_FILE_RANGE
	// this is best-effort. ignore errors
	::sync_file_range(m_file.fd(), start - static_cast<const byte*>(m_mapping)
		, size, SYNC_FILE_RANGE_WRITE);
#endif

	// msync(MS_ASYNC) is a no-op on Linux > 2.6.19.
	::msync(start, size, MS_ASYNC);

#endif // MAP_VIEW_OF_FILE
}

} // aux
} // libtorrent

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

