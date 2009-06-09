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
#include "libtorrent/alloca.hpp"

#include <boost/scoped_ptr.hpp>
#ifdef TORRENT_WINDOWS
// windows part
#include "libtorrent/utf8.hpp"

#include <windows.h>
#include <winioctl.h>

#else
// posix part
#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <errno.h>

#include <boost/static_assert.hpp>
// make sure the _FILE_OFFSET_BITS define worked
// on this platform. It's supposed to make file
// related functions support 64-bit offsets.
// this test makes sure lseed() returns a type
// at least 64 bits wide
BOOST_STATIC_ASSERT(sizeof(lseek(0, 0, 0)) >= 8);

#endif

#include <boost/filesystem/operations.hpp>
#include "libtorrent/file.hpp"
#include <cstring>
#include <vector>

#if TORRENT_USE_WPATH || TORRENT_USE_LOCALE_FILENAMES
// for convert_to_wstring and convert_to_native
#include "libtorrent/escape_string.hpp"
#endif

#include "libtorrent/assert.hpp"

#ifdef TORRENT_DEBUG
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::no_buffer) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::attribute_mask) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::no_buffer & libtorrent::file::attribute_mask) == 0);
#endif

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
#endif

}

namespace libtorrent
{
	namespace fs = boost::filesystem;

	file::file()
#ifdef TORRENT_WINDOWS
		: m_file_handle(INVALID_HANDLE_VALUE)
#else
		: m_fd(-1)
#endif
		, m_open_mode(0)
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		, m_sector_size(0)
#endif
	{}

	file::file(fs::path const& path, int mode, error_code& ec)
#ifdef TORRENT_WINDOWS
		: m_file_handle(INVALID_HANDLE_VALUE)
#else
		: m_fd(-1)
#endif
		, m_open_mode(0)
	{
		open(path, mode, ec);
	}

	file::~file()
	{
		close();
	}

	bool file::open(fs::path const& path, int mode, error_code& ec)
	{
		close();
#ifdef TORRENT_WINDOWS

		struct open_mode_t
		{
			DWORD rw_mode;
			DWORD share_mode;
			DWORD create_mode;
			DWORD flags;
		};

		const static open_mode_t mode_array[] =
		{
			// read_only
			{GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS},
			// write_only
			{GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS},
			// read_write
			{GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS},
			// invalid option
			{0,0,0,0},
			// read_only no_buffer
			{GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING },
			// write_only no_buffer
			{GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING },
			// read_write no_buffer
			{GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING },
			// invalid option
			{0,0,0,0}
		};

		const static DWORD attrib_array[] =
		{
			FILE_ATTRIBUTE_NORMAL, // no attrib
			FILE_ATTRIBUTE_HIDDEN, // hidden
			FILE_ATTRIBUTE_NORMAL, // executable
			FILE_ATTRIBUTE_HIDDEN, // hidden + executable
		};

#if TORRENT_USE_WPATH
		m_path = convert_to_wstring(path.external_file_string());
#else
		m_path = utf8_native(path.external_file_string());
#endif

		TORRENT_ASSERT((mode & mode_mask) < sizeof(mode_array)/sizeof(mode_array[0]));
		open_mode_t const& m = mode_array[mode & mode_mask];
		DWORD a = attrib_array[(mode & attribute_mask) >> 12];

		m_file_handle = CreateFile(m_path.c_str(), m.rw_mode, m.share_mode, 0
			, m.create_mode, m.flags | (a ? a : FILE_ATTRIBUTE_NORMAL), 0);

		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return false;
		}

		// try to make the file sparse if supported
		if (mode & file::sparse)
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

		if (mode & attribute_executable)
			permissions |= S_IXGRP | S_IXOTH | S_IXUSR;

		static const int mode_array[] = {O_RDONLY, O_WRONLY | O_CREAT, O_RDWR | O_CREAT};
#ifdef O_DIRECT
		static const int no_buffer_flag[] = {0, O_DIRECT};
#else
		static const int no_buffer_flag[] = {0, 0};
#endif

#if TORRENT_USE_LOCALE_FILENAMES
 		m_fd = ::open(convert_to_native(path.external_file_string()).c_str()
 			, mode_array[mode & rw_mask] | no_buffer_flag[(mode & no_buffer) >> 2], permissions);
#else
 		m_fd = ::open(path.external_file_string().c_str()
 			, mode_array[mode & rw_mask] | no_buffer_flag[(mode & no_buffer) >> 2], permissions);
#endif

#ifdef TORRENT_LINUX
		// workaround for linux bug
		// https://bugs.launchpad.net/ubuntu/+source/linux/+bug/269946
		if (m_fd == -1 && (mode & no_buffer) && errno == EINVAL)
		{
			mode &= ~no_buffer;
			m_fd = ::open(path.external_file_string().c_str()
				, mode & (rw_mask | no_buffer), permissions);
		}

#endif
		if (m_fd == -1)
		{
			ec = error_code(errno, get_posix_category());
			TORRENT_ASSERT(ec);
			return false;
		}

#ifdef F_NOCACHE
		if (mode & no_buffer)
		{
			int yes = 1;
			fcntl(m_fd, F_NOCACHE, &yes);
		}
#endif

#ifdef POSIX_FADV_RANDOM
		// disable read-ahead
		posix_fadvise(m_fd, 0, 0, POSIX_FADV_RANDOM);
#endif

#endif
		m_open_mode = mode;

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

	int file::pos_alignment() const
	{
		// on linux and windows, file offsets needs
		// to be aligned to the disk sector size
#if defined TORRENT_LINUX
		if (m_sector_size == 0)
		{
			struct statvfs fs;
			if (fstatvfs(m_fd, &fs) == 0)
				m_sector_size = fs.f_bsize;
			else
				m_sector_size = 4096;
		}	
		return m_sector_size;
#elif defined TORRENT_WINDOWS
		if (m_sector_size == 0)
		{
			DWORD sectors_per_cluster;
			DWORD bytes_per_sector;
			DWORD free_clusters;
			DWORD total_clusters;
#if TORRENT_USE_WPATH
			wchar_t backslash = L'\\';
#else
			char backslash = '\\';
#endif
			if (GetDiskFreeSpace(m_path.substr(0, m_path.find_first_of(backslash)+1).c_str()
				, &sectors_per_cluster, &bytes_per_sector
				, &free_clusters, &total_clusters))
				m_sector_size = bytes_per_sector;
			else
				m_sector_size = 4096;
		}
		return m_sector_size;
#else
		return 1;
#endif
	}

	int file::buf_alignment() const
	{
#if defined TORRENT_WINDOWS
		init_file();
		return m_page_size;
#else
		return pos_alignment();
#endif
	}

	int file::size_alignment() const
	{
#if defined TORRENT_WINDOWS
		init_file();
		return m_page_size;
#else
		return pos_alignment();
#endif
	}

	void file::close()
	{
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		m_sector_size = 0;
#endif

#ifdef TORRENT_WINDOWS
		if (m_file_handle == INVALID_HANDLE_VALUE) return;
		CloseHandle(m_file_handle);
		m_file_handle = INVALID_HANDLE_VALUE;
		m_path.clear();
#else
		if (m_fd == -1) return;
		::close(m_fd);
		m_fd = -1;
#endif
		m_open_mode = 0;
	}

	// defined in storage.cpp
	int bufs_size(file::iovec_t const* bufs, int num_bufs);
	
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX || defined TORRENT_DEBUG

	int file::m_page_size = 0;

	void file::init_file()
	{
		if (m_page_size != 0) return;

#ifdef TORRENT_WINDOWS
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		m_page_size = si.dwPageSize;
#else
		m_page_size = sysconf(_SC_PAGESIZE);
#endif
	}

#endif

	size_type file::readv(size_type file_offset, iovec_t const* bufs, int num_bufs, error_code& ec)
	{
		TORRENT_ASSERT((m_open_mode & rw_mask) == read_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(is_open());

#if defined TORRENT_WINDOWS || defined TORRENT_LINUX || defined TORRENT_DEBUG
		// make sure m_page_size is initialized
		init_file();
#endif

#ifdef TORRENT_DEBUG
		if (m_open_mode & no_buffer)
		{
			bool eof = false;
			int size = 0;
			// when opened in no_buffer mode, the file_offset must
			// be aligned to pos_alignment()
			TORRENT_ASSERT((file_offset & (pos_alignment()-1)) == 0);
			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				TORRENT_ASSERT((uintptr_t(i->iov_base) & (buf_alignment()-1)) == 0);
				// every buffer must be a multiple of the page size
				// except for the last one
				TORRENT_ASSERT((i->iov_len & (size_alignment()-1)) == 0 || i == end-1);
				if ((i->iov_len & (size_alignment()-1)) != 0) eof = true;
				size += i->iov_len;
			}
			error_code code;
			if (eof) TORRENT_ASSERT(file_offset + size >= get_size(code));
		}
#endif

#ifdef TORRENT_WINDOWS

		DWORD ret = 0;

		// since the ReadFileScatter requires the file to be opened
		// with no buffering, and no buffering requires page aligned
		// buffers, open the file in non-buffered mode in case the
		// buffer is not aligned. Most of the times the buffer should
		// be aligned though

		if ((m_open_mode & no_buffer) == 0)
		{
			// this means the buffer base or the buffer size is not aligned
			// to the page size. Use a regular file for this operation.

			LARGE_INTEGER offs;
			offs.QuadPart = file_offset;
			if (SetFilePointerEx(m_file_handle, offs, &offs, FILE_BEGIN) == FALSE)
			{
				ec = error_code(GetLastError(), get_system_category());
				return -1;
			}

			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				DWORD intermediate = 0;
				if (ReadFile(m_file_handle, (char*)i->iov_base
					, (DWORD)i->iov_len, &intermediate, 0) == FALSE)
				{
					ec = error_code(GetLastError(), get_system_category());
					return -1;
				}
				ret += intermediate;
			}
			return ret;
		}

		int size = bufs_size(bufs, num_bufs);
		// number of pages for the read. round up
		int num_pages = (size + m_page_size - 1) / m_page_size;
		// allocate array of FILE_SEGMENT_ELEMENT for ReadFileScatter
		FILE_SEGMENT_ELEMENT* segment_array = TORRENT_ALLOCA(FILE_SEGMENT_ELEMENT, num_pages + 1);
		FILE_SEGMENT_ELEMENT* cur_seg = segment_array;

		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			for (int k = 0; k < i->iov_len; k += m_page_size)
			{
				cur_seg->Buffer = ((char*)i->iov_base) + k;
				++cur_seg;
			}
		}
		// terminate the array
		cur_seg->Buffer = 0;

		OVERLAPPED ol;
		ol.Internal = 0;
		ol.InternalHigh = 0;
		ol.OffsetHigh = file_offset >> 32;
		ol.Offset = file_offset & 0xffffffff;
		ol.hEvent = CreateEvent(0, true, false, 0);

		ret += size;
		size = num_pages * m_page_size;
		if (ReadFileScatter(m_file_handle, segment_array, size, 0, &ol) == 0)
		{
			DWORD last_error = GetLastError();
			if (last_error != ERROR_IO_PENDING)
			{
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (GetOverlappedResult(m_file_handle, &ol, &ret, true) == 0)
			{
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
		}
		CloseHandle(ol.hEvent);
		return ret;

#else // TORRENT_WINDOWS

		size_type ret = lseek(m_fd, file_offset, SEEK_SET);
		if (ret < 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}
#if TORRENT_USE_READV

#ifdef TORRENT_LINUX
		bool aligned = false;
		int size = 0;
		// if we're not opened in no-buffer mode, we don't need alignment
		if ((m_open_mode & no_buffer) == 0) aligned = true;
		if (!aligned)
		{
			size = bufs_size(bufs, num_bufs);
			if (size & (size_alignment()-1) == 0) aligned = true;
		}
		if (aligned)
#endif // TORRENT_LINUX
		{
			ret = ::readv(m_fd, bufs, num_bufs);
			if (ret < 0)
			{
				ec = error_code(errno, get_posix_category());
				return -1;
			}
			return ret;
		}
#ifdef TORRENT_LINUX
		file::iovec_t* temp_bufs = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		memcpy(temp_bufs, bufs, sizeof(file::iovec_t) * num_bufs);
		iovec_t& last = temp_bufs[num_bufs-1];
		last.iov_len = (last.iov_len & ~(size_alignment()-1)) + m_page_size;
		ret = ::readv(m_fd, temp_bufs, num_bufs);
		if (ret < 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}
		return (std::min)(ret, size_type(size));
#endif // TORRENT_LINUX

#else // TORRENT_USE_READV

		ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int tmp = read(m_fd, i->iov_base, i->iov_len);
			if (tmp < 0)
			{
				ec = error_code(errno, get_posix_category());
				return -1;
			}
			ret += tmp;
			if (tmp < i->iov_len) break;
		}
		return ret;

#endif // TORRENT_USE_READV

#endif // TORRENT_WINDOWS
	}

	size_type file::writev(size_type file_offset, iovec_t const* bufs, int num_bufs, error_code& ec)
	{
		TORRENT_ASSERT((m_open_mode & rw_mask) == write_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(is_open());

#if defined TORRENT_WINDOWS || defined TORRENT_LINUX || defined TORRENT_DEBUG
		// make sure m_page_size is initialized
		init_file();
#endif

#ifdef TORRENT_DEBUG
		if (m_open_mode & no_buffer)
		{
			bool eof = false;
			int size = 0;
			// when opened in no_buffer mode, the file_offset must
			// be aligned to pos_alignment()
			TORRENT_ASSERT((file_offset & (pos_alignment()-1)) == 0);
			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				TORRENT_ASSERT((uintptr_t(i->iov_base) & (buf_alignment()-1)) == 0);
				// every buffer must be a multiple of the page size
				// except for the last one
				TORRENT_ASSERT((i->iov_len & (size_alignment()-1)) == 0 || i == end-1);
				if ((i->iov_len & (size_alignment()-1)) != 0) eof = true;
				size += i->iov_len;
			}
			error_code code;
			if (eof) TORRENT_ASSERT(file_offset + size >= get_size(code));
		}
#endif

#ifdef TORRENT_WINDOWS

		DWORD ret = 0;

		// since the ReadFileScatter requires the file to be opened
		// with no buffering, and no buffering requires page aligned
		// buffers, open the file in non-buffered mode in case the
		// buffer is not aligned. Most of the times the buffer should
		// be aligned though

		if ((m_open_mode & no_buffer) == 0)
		{
			// this means the buffer base or the buffer size is not aligned
			// to the page size. Use a regular file for this operation.

			LARGE_INTEGER offs;
			offs.QuadPart = file_offset;
			if (SetFilePointerEx(m_file_handle, offs, &offs, FILE_BEGIN) == FALSE)
			{
				ec = error_code(GetLastError(), get_system_category());
				return -1;
			}

			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				DWORD intermediate = 0;
				if (WriteFile(m_file_handle, (char const*)i->iov_base
					, (DWORD)i->iov_len, &intermediate, 0) == FALSE)
				{
					ec = error_code(GetLastError(), get_system_category());
					return -1;
				}
				ret += intermediate;
			}
			return ret;
		}

		int size = bufs_size(bufs, num_bufs);
		// number of pages for the write. round up
		int num_pages = (size + m_page_size - 1) / m_page_size;
		// allocate array of FILE_SEGMENT_ELEMENT for WriteFileGather
		FILE_SEGMENT_ELEMENT* segment_array = TORRENT_ALLOCA(FILE_SEGMENT_ELEMENT, num_pages + 1);
		FILE_SEGMENT_ELEMENT* cur_seg = segment_array;

		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			for (int k = 0; k < i->iov_len; k += m_page_size)
			{
				cur_seg->Buffer = ((char*)i->iov_base) + k;
				++cur_seg;
			}
		}
		// terminate the array
		cur_seg->Buffer = 0;

		OVERLAPPED ol;
		ol.Internal = 0;
		ol.InternalHigh = 0;
		ol.OffsetHigh = file_offset >> 32;
		ol.Offset = file_offset & 0xffffffff;
		ol.hEvent = CreateEvent(0, true, false, 0);

		ret += size;
		// if file_size is > 0, the file will be opened in unbuffered
		// mode after the write completes, and truncate the file to
		// file_size.
		size_type file_size = 0;
	
		if ((size & (m_page_size-1)) != 0)
		{
			// if size is not an even multiple, this must be the tail
			// of the file. Write the whole page and then open a new
			// file without FILE_FLAG_NO_BUFFERING and set the
			// file size to file_offset + size

			file_size = file_offset + size;
			size = num_pages * m_page_size;
		}

		if (WriteFileGather(m_file_handle, segment_array, size, 0, &ol) == 0)
		{
			if (GetLastError() != ERROR_IO_PENDING)
			{
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			DWORD tmp;
			if (GetOverlappedResult(m_file_handle, &ol, &tmp, true) == 0)
			{
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (tmp < ret) ret = tmp;
		}
		CloseHandle(ol.hEvent);

		if (file_size > 0)
		{
			HANDLE f = CreateFile(m_path.c_str(), GENERIC_WRITE
			, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING
			, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);

			if (f == INVALID_HANDLE_VALUE)
			{
				ec = error_code(GetLastError(), get_system_category());
				return -1;
			}

			LARGE_INTEGER offs;
			offs.QuadPart = file_size;
			if (SetFilePointerEx(f, offs, &offs, FILE_BEGIN) == FALSE)
			{
				CloseHandle(f);
				ec = error_code(GetLastError(), get_system_category());
				return -1;
			}
			if (::SetEndOfFile(f) == FALSE)
			{
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(f);
				return -1;
			}
			CloseHandle(f);
		}

		return ret;
#else
		size_type ret = lseek(m_fd, file_offset, SEEK_SET);
		if (ret < 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}

#if TORRENT_USE_WRITEV

#ifdef TORRENT_LINUX
		bool aligned = false;
		int size = 0;
		// if we're not opened in no-buffer mode, we don't need alignment
		if ((m_open_mode & no_buffer) == 0) aligned = true;
		if (!aligned)
		{
			size = bufs_size(bufs, num_bufs);
			if (size & (size_alignment()-1) == 0) aligned = true;
		}
		if (aligned)
#endif
		{
			ret = ::writev(m_fd, bufs, num_bufs);
			if (ret < 0)
			{
				ec = error_code(errno, get_posix_category());
				return -1;
			}
			return ret;
		}
#ifdef TORRENT_LINUX
		file::iovec_t* temp_bufs = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		memcpy(temp_bufs, bufs, sizeof(file::iovec_t) * num_bufs);
		iovec_t& last = temp_bufs[num_bufs-1];
		last.iov_len = (last.iov_len & ~(size_alignment()-1)) + size_alignment();
		ret = ::writev(m_fd, temp_bufs, num_bufs);
		if (ret < 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}
		if (ftruncate(m_fd, file_offset + size) < 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}
		return (std::min)(ret, size_type(size));
#endif // TORRENT_LINUX

#else // TORRENT_USE_WRITEV

		ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int tmp = write(m_fd, i->iov_base, i->iov_len);
			if (tmp < 0)
			{
				ec = error_code(errno, get_posix_category());
				return -1;
			}
			ret += tmp;
			if (tmp < i->iov_len) break;
		}
		return ret;

#endif // TORRENT_USE_WRITEV

#endif // TORRENT_WINDOWS
	}

  	bool file::set_size(size_type s, error_code& ec)
  	{
  		TORRENT_ASSERT(is_open());
  		TORRENT_ASSERT(s >= 0);

#ifdef TORRENT_WINDOWS
		LARGE_INTEGER offs;
		offs.QuadPart = s;
		if (SetFilePointerEx(m_file_handle, offs, &offs, FILE_BEGIN) == FALSE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return false;
		}
#if _WIN32_WINNT >= 0x501
		if ((m_open_mode & sparse) == 0)
		{
			// if the user has permissions, avoid filling
			// the file with zeroes, but just fill it with
			// garbage instead
			SetFileValidData(m_file_handle, offs.QuadPart);
		}
#endif
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
		if ((m_open_mode & sparse) == 0)
		{
			// if we're not in sparse mode, allocate the storage
#ifdef F_PREALLOCATE
			fstore_t f = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, s, 0};
			if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
			{
				ec = error_code(errno, get_posix_category());
				return false;
			}
#else
			int ret = posix_fallocate(m_fd, 0, s);
			if (ret != 0)
			{
				ec = error_code(ret, get_posix_category());
				return false;
			}
#endif
		}
#endif
		return true;
	}

	size_type file::get_size(error_code& ec) const
	{
#ifdef TORRENT_WINDOWS
		LARGE_INTEGER file_size;
		if (!GetFileSizeEx(m_file_handle, &file_size))
		{
			ec = error_code(GetLastError(), get_system_category());
			return -1;
		}
		return file_size.QuadPart;
#else
		struct stat fs;
		if (fstat(m_fd, &fs) != 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}
		return fs.st_size;
#endif
	}

	size_type file::sparse_end(size_type start) const
	{
#ifdef TORRENT_WINDOWS
		FILE_ALLOCATED_RANGE_BUFFER buffer;
		DWORD bytes_returned = 0;
		FILE_ALLOCATED_RANGE_BUFFER in;
		error_code ec;
		size_type file_size = get_size(ec);
		if (ec) return start;
		in.FileOffset.QuadPart = start;
		in.Length.QuadPart = file_size - start;
		if (!DeviceIoControl(m_file_handle, FSCTL_QUERY_ALLOCATED_RANGES
			, &in, sizeof(FILE_ALLOCATED_RANGE_BUFFER)
			, &buffer, sizeof(FILE_ALLOCATED_RANGE_BUFFER), &bytes_returned, 0))
		{
			int err = GetLastError();
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return start;
		}

		// if there are no allocated regions within the rest
		// of the file, return the end of the file
		if (bytes_returned == 0) return file_size;

		// assume that this range overlaps the start of the
		// region we were interested in, and that start actually
		// resides in an allocated region.
		if (buffer.FileOffset.QuadPart < start) return start;

		// return the offset to the next allocated region
		return buffer.FileOffset.QuadPart;
		
#elif defined SEEK_DATA
		// this is supported on solaris
		size_type ret = lseek(m_fd, start, SEEK_DATA);
		if (ret < 0) return start;
#else
		return start;
#endif
	}

}

