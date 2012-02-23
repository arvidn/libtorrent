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

/*
	Physical file offset patch by Morten Husveit
*/

#include "libtorrent/pch.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/allocator.hpp" // page_size

#include <boost/scoped_ptr.hpp>
#ifdef TORRENT_WINDOWS
// windows part

#include "libtorrent/utf8.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#ifndef PtrToPtr64
#define PtrToPtr64(x) (x)
#endif

#else
// posix part
#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <fcntl.h> // for F_LOG2PHYS
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <errno.h>
#ifdef TORRENT_LINUX
#include <sys/ioctl.h>
#ifdef HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h> // FIEMAP_*
#include <linux/fs.h>  // FS_IOC_FIEMAP
#endif

#include <asm/unistd.h> // For __NR_fallocate

// circumvent the lack of support in glibc
static int my_fallocate(int fd, int mode, loff_t offset, loff_t len)
{
#ifdef __NR_fallocate
	// the man page on fallocate differes between versions of linux.
	// it appears that fallocate in fact sets errno and returns -1
	// on failure.
	return syscall(__NR_fallocate, fd, mode, offset, len);
#else
	// pretend that the system call doesn't exist
	errno = ENOSYS;
	return -1;
#endif
}

#endif // TORRENT_LINUX

#undef _FILE_OFFSET_BITS

#include <boost/static_assert.hpp>
// make sure the _FILE_OFFSET_BITS define worked
// on this platform. It's supposed to make file
// related functions support 64-bit offsets.
// this test makes sure lseek() returns a type
// at least 64 bits wide
BOOST_STATIC_ASSERT(sizeof(lseek(0, 0, 0)) >= 8);

#endif

#include <boost/filesystem/operations.hpp>
#include "libtorrent/file.hpp"
#include <cstring>
#include <vector>

// for convert_to_wstring and convert_to_native
#include "libtorrent/escape_string.hpp"

#include "libtorrent/assert.hpp"

#ifdef TORRENT_DEBUG
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::no_buffer) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::attribute_mask) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::no_buffer & libtorrent::file::attribute_mask) == 0);
#endif

#ifdef TORRENT_WINDOWS
#if defined UNICODE && !TORRENT_USE_WPATH
#warning wide character support not available. Files will be saved using narrow string names
#endif
#endif // TORRENT_WINDOWS

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
			{GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0},
			// write_only
			{GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS, 0},
			// read_write
			{GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, OPEN_ALWAYS, 0},
			// invalid option
			{0,0,0,0},
			// read_only no_buffer
			{GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING },
			// write_only no_buffer
			{GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING },
			// read_write no_buffer
			{GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING },
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
#define CreateFile_ CreateFileW
		m_path = convert_to_wstring(path.external_file_string());
#else
#define CreateFile_ CreateFileA
		m_path = convert_to_native(path.external_file_string());
#endif

		TORRENT_ASSERT((mode & mode_mask) < sizeof(mode_array)/sizeof(mode_array[0]));
		open_mode_t const& m = mode_array[mode & mode_mask];
		DWORD a = attrib_array[(mode & attribute_mask) >> 12];

		DWORD extra_flags = ((mode & random_access) ? FILE_FLAG_RANDOM_ACCESS : 0)
			| (a ? a : FILE_ATTRIBUTE_NORMAL);

		m_file_handle = CreateFile_(m_path.c_str(), m.rw_mode, m.share_mode, 0
			, m.create_mode, m.flags | extra_flags, 0);

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

 		m_fd = ::open(convert_to_native(path.external_file_string()).c_str()
 			, mode_array[mode & rw_mask] | no_buffer_flag[(mode & no_buffer) >> 2], permissions);

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
		if (mode & random_access)
		{
			// disable read-ahead
			posix_fadvise(m_fd, 0, 0, POSIX_FADV_RANDOM);
		}
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
#define GetDiskFreeSpace_ GetDiskFreeSpaceW
			wchar_t backslash = L'\\';
#else
#define GetDiskFreeSpace_ GetDiskFreeSpaceA
			char backslash = '\\';
#endif
			if (GetDiskFreeSpace_(m_path.substr(0, m_path.find_first_of(backslash)+1).c_str()
				, &sectors_per_cluster, &bytes_per_sector
				, &free_clusters, &total_clusters))
			{
				m_sector_size = bytes_per_sector;
				m_cluster_size = sectors_per_cluster * bytes_per_sector;
			}
			else
			{
				// make a conservative guess
				m_sector_size = 512;
				m_cluster_size = 4096;
			}
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

		m_page_size = page_size();
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
				cur_seg->Buffer = PtrToPtr64((((char*)i->iov_base) + k));
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
				TORRENT_ASSERT(GetLastError() != ERROR_BAD_ARGUMENTS);
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			DWORD num_read;
			if (GetOverlappedResult(m_file_handle, &ol, &num_read, true) == 0)
			{
				if (GetLastError() != ERROR_HANDLE_EOF)
				{
					ec = error_code(GetLastError(), get_system_category());
					CloseHandle(ol.hEvent);
					return -1;
				}
			}
			if (num_read < ret) ret = num_read;
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

		ret = 0;
		while (num_bufs > 0)
		{
			int nbufs = (std::min)(num_bufs, TORRENT_IOV_MAX);
			int tmp_ret = 0;
#ifdef TORRENT_LINUX
			bool aligned = false;
			int size = 0;
			// if we're not opened in no-buffer mode, we don't need alignment
			if ((m_open_mode & no_buffer) == 0) aligned = true;
			if (!aligned)
			{
				size = bufs_size(bufs, nbufs);
				if ((size & (size_alignment()-1)) == 0) aligned = true;
			}
			if (aligned)
#endif // TORRENT_LINUX
			{
				tmp_ret = ::readv(m_fd, bufs, nbufs);
				if (tmp_ret < 0)
				{
					ec = error_code(errno, get_posix_category());
					return -1;
				}
				ret += tmp_ret;
			}
#ifdef TORRENT_LINUX
			else
			{
				file::iovec_t* temp_bufs = TORRENT_ALLOCA(file::iovec_t, nbufs);
				memcpy(temp_bufs, bufs, sizeof(file::iovec_t) * nbufs);
				iovec_t& last = temp_bufs[nbufs-1];
				last.iov_len = (last.iov_len & ~(size_alignment()-1)) + m_page_size;
				tmp_ret = ::readv(m_fd, temp_bufs, nbufs);
				if (tmp_ret < 0)
				{
					ec = error_code(errno, get_posix_category());
					return -1;
				}
			}
			ret += (std::min)(tmp_ret, size);

#endif // TORRENT_LINUX

			num_bufs -= nbufs;
			bufs += nbufs;
		}

		return ret;

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
			if (eof) 
			{
				size_type fsize = get_size(code);
				if (code) printf("get_size: %s\n", code.message().c_str());
				if (file_offset + size < fsize)
				{
					printf("offset: %d size: %d get_size: %d\n", int(file_offset), int(size), int(fsize));
					TORRENT_ASSERT(false);
				}
			}
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
				cur_seg->Buffer = PtrToPtr64((((char*)i->iov_base) + k));
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
		size_type file_size = 0;
	
		if ((size & (m_page_size-1)) != 0)
		{
			// if size is not an even multiple, this must be the tail
			// of the file.

			file_size = file_offset + size;
			size = num_pages * m_page_size;
		}

		if (WriteFileGather(m_file_handle, segment_array, size, 0, &ol) == 0)
		{
			if (GetLastError() != ERROR_IO_PENDING)
			{
				TORRENT_ASSERT(GetLastError() != ERROR_BAD_ARGUMENTS);
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			DWORD num_written;
			if (GetOverlappedResult(m_file_handle, &ol, &num_written, true) == 0)
			{
				ec = error_code(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (num_written < ret) ret = num_written;
		}
		CloseHandle(ol.hEvent);

		if (file_size > 0) set_size(file_size, ec);
		return ret;
#else
		size_type ret = lseek(m_fd, file_offset, SEEK_SET);
		if (ret < 0)
		{
			ec = error_code(errno, get_posix_category());
			return -1;
		}

#if TORRENT_USE_WRITEV

		ret = 0;
		while (num_bufs > 0)
		{
			int nbufs = (std::min)(num_bufs, TORRENT_IOV_MAX);
			int tmp_ret = 0;
#ifdef TORRENT_LINUX
			bool aligned = false;
			int size = 0;
			// if we're not opened in no-buffer mode, we don't need alignment
			if ((m_open_mode & no_buffer) == 0) aligned = true;
			if (!aligned)
			{
				size = bufs_size(bufs, nbufs);
				if ((size & (size_alignment()-1)) == 0) aligned = true;
			}
			if (aligned)
#endif
			{
				tmp_ret = ::writev(m_fd, bufs, nbufs);
				if (tmp_ret < 0)
				{
					ec = error_code(errno, get_posix_category());
					return -1;
				}
				ret += tmp_ret;
			}
#ifdef TORRENT_LINUX
			else
			{
				file::iovec_t* temp_bufs = TORRENT_ALLOCA(file::iovec_t, nbufs);
				memcpy(temp_bufs, bufs, sizeof(file::iovec_t) * nbufs);
				iovec_t& last = temp_bufs[nbufs-1];
				last.iov_len = (last.iov_len & ~(size_alignment()-1)) + size_alignment();
				tmp_ret = ::writev(m_fd, temp_bufs, nbufs);
				if (tmp_ret < 0)
				{
					ec = error_code(errno, get_posix_category());
					return -1;
				}
				if (ftruncate(m_fd, file_offset + size) < 0)
				{
					ec = error_code(errno, get_posix_category());
					return -1;
				}
				ret += (std::min)(tmp_ret, size);
			}
#endif // TORRENT_LINUX

			num_bufs -= nbufs;
			bufs += nbufs;
		}

		return ret;

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

	size_type file::phys_offset(size_type offset)
	{
#ifdef FIEMAP_EXTENT_UNKNOWN
		// for documentation of this feature
		// http://lwn.net/Articles/297696/
		struct
		{
			struct fiemap fiemap;
			struct fiemap_extent extent;
		} fm;

		memset(&fm, 0, sizeof(fm));
		fm.fiemap.fm_start = offset;
		fm.fiemap.fm_length = size_alignment();
		// this sounds expensive
		fm.fiemap.fm_flags = FIEMAP_FLAG_SYNC;
		fm.fiemap.fm_extent_count = 1;

		if (ioctl(m_fd, FS_IOC_FIEMAP, &fm) == -1)
			return 0;

		if (fm.fiemap.fm_extents[0].fe_flags & FIEMAP_EXTENT_UNKNOWN)
			return 0;

		// the returned extent is not guaranteed to start
		// at the requested offset, adjust for that in
		// case they differ
		TORRENT_ASSERT(offset >= fm.fiemap.fm_extents[0].fe_logical);
		return fm.fiemap.fm_extents[0].fe_physical + (offset - fm.fiemap.fm_extents[0].fe_logical);

#elif defined F_LOG2PHYS
		// for documentation of this feature
		// http://developer.apple.com/mac/library/documentation/Darwin/Reference/ManPages/man2/fcntl.2.html

		log2phys l;
		size_type ret = lseek(m_fd, offset, SEEK_SET);
		if (ret < 0) return 0;
		if (fcntl(m_fd, F_LOG2PHYS, &l) == -1) return 0;
		return l.l2p_devoffset;
#elif defined TORRENT_WINDOWS
		// for documentation of this feature
		// http://msdn.microsoft.com/en-us/library/aa364572(VS.85).aspx
		STARTING_VCN_INPUT_BUFFER in;
		RETRIEVAL_POINTERS_BUFFER out;
		DWORD out_bytes;

		// query cluster size
		pos_alignment();
		in.StartingVcn.QuadPart = offset / m_cluster_size;
		int cluster_offset = in.StartingVcn.QuadPart % m_cluster_size;

		if (DeviceIoControl(m_file_handle, FSCTL_GET_RETRIEVAL_POINTERS, &in
			, sizeof(in), &out, sizeof(out), &out_bytes, 0) == 0)
		{
			DWORD error = GetLastError();
			TORRENT_ASSERT(error != ERROR_INVALID_PARAMETER);

			// insufficient buffer error is expected, but we're
			// only interested in the first extent anyway
			if (error != ERROR_MORE_DATA) return 0;
		}
		if (out_bytes < sizeof(out)) return 0;
		if (out.ExtentCount == 0) return 0;
		if (out.Extents[0].Lcn.QuadPart == (LONGLONG)-1) return 0;
		TORRENT_ASSERT(in.StartingVcn.QuadPart >= out.StartingVcn.QuadPart);
		return (out.Extents[0].Lcn.QuadPart
			+ (in.StartingVcn.QuadPart - out.StartingVcn.QuadPart))
			* m_cluster_size + cluster_offset;
#endif
		return 0;
	}

  	bool file::set_size(size_type s, error_code& ec)
  	{
  		TORRENT_ASSERT(is_open());
  		TORRENT_ASSERT(s >= 0);

#ifdef TORRENT_WINDOWS

		if ((m_open_mode & no_buffer) && (s & (size_alignment()-1)) != 0)
		{
			// the file is opened in unbuffered mode, and the size is not
			// aligned to the required cluster size. Use NtSetInformationFile

#define FileEndOfFileInformation 20
#ifndef NT_SUCCESS
#define NT_SUCCESS(x) (!((x) & 0x80000000))
#endif
			
			// for NtSetInformationFile, see: 
			// http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtSetInformationFile.html

			typedef DWORD _NTSTATUS;
			typedef _NTSTATUS (NTAPI * NtSetInformationFile_t)(HANDLE file, PULONG_PTR iosb, PVOID data, ULONG len, ULONG file_info_class);

			static NtSetInformationFile_t NtSetInformationFile = 0;
			static bool failed_ntdll = false;

			if (NtSetInformationFile == 0 && !failed_ntdll)
			{
				HMODULE nt = LoadLibraryA("ntdll");
				if (nt)
				{
					NtSetInformationFile = (NtSetInformationFile_t)GetProcAddress(nt, "NtSetInformationFile");
					if (NtSetInformationFile == 0) failed_ntdll = true;
				}
				else failed_ntdll = true;
			}

			if (!failed_ntdll && NtSetInformationFile)
			{
				ULONG_PTR Iosb[2];
				LARGE_INTEGER fsize;
				fsize.QuadPart = s;
				_NTSTATUS st = NtSetInformationFile(m_file_handle
					, Iosb, &fsize, sizeof(fsize), FileEndOfFileInformation);
				if (!NT_SUCCESS(st)) 
				{
					ec.assign(INVALID_SET_FILE_POINTER, get_system_category());
					return false;
				}
				return true;
			}

			// couldn't find ntdll or NtSetFileInformation function
			// and the file is opened in unbuffered mode! There's
			// nothing we can do! (short of re-opening the file, but
			// that introduces all sorts of nasty race conditions)
			return false;
		}

		LARGE_INTEGER offs;
		LARGE_INTEGER cur_size;
		if (GetFileSizeEx(m_file_handle, &cur_size) == FALSE)
		{
			ec = error_code(GetLastError(), get_system_category());
			return false;
		}
		offs.QuadPart = s;
		// only set the file size if it's not already at
		// the right size. We don't want to update the
		// modification time if we don't have to
		if (cur_size.QuadPart != s)
		{
			if (SetFilePointerEx(m_file_handle, offs, &offs, FILE_BEGIN) == FALSE)
			{
				ec.assign(GetLastError(), get_system_category());
				return false;
			}
			if (::SetEndOfFile(m_file_handle) == FALSE)
			{
				ec.assign(GetLastError(), get_system_category());
				return false;
			}
		}
#if _WIN32_WINNT >= 0x501		
		if ((m_open_mode & sparse) == 0)
		{
			// only allocate the space if the file
			// is not fully allocated
			DWORD high_dword = 0;
			offs.LowPart = GetCompressedFileSize(m_path.c_str(), &high_dword);
			offs.HighPart = high_dword;
			ec.assign(GetLastError(), get_system_category());
			if (ec) return false;
			if (offs.QuadPart != s)
			{
				// if the user has permissions, avoid filling
				// the file with zeroes, but just fill it with
				// garbage instead
				SetFileValidData(m_file_handle, offs.QuadPart);
			}
		}
#endif // _WIN32_WINNT >= 0x501
#else // NON-WINDOWS
		struct stat st;
		if (fstat(m_fd, &st) != 0)
		{
			ec.assign(errno, get_posix_category());
			return false;
		}

		// only truncate the file if it doesn't already
		// have the right size. We don't want to update
		if (st.st_size != s && ftruncate(m_fd, s) < 0)
		{
			ec.assign(errno, get_posix_category());
			return false;
		}

		// if we're not in sparse mode, allocate the storage
		// but only if the number of allocated blocks for the file
		// is less than the file size. Otherwise we would just
		// update the modification time of the file for no good
		// reason.
		if ((m_open_mode & sparse) == 0
			&& st.st_blocks < (s + st.st_blksize - 1) / st.st_blksize)
		{
			// How do we know that the file is already allocated?
			// if we always try to allocate the space, we'll update
			// the modification time without actually changing the file
			// but if we don't do anything if the file size is
#ifdef F_PREALLOCATE
			fstore_t f = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, s, 0};
			if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
			{
				if (errno != ENOSPC)
				{
					ec.assign(errno, get_posix_category());
					return false;
				}
				// ok, let's try to allocate non contiguous space then
				fstore_t f = {F_ALLOCATEALL, F_PEOFPOSMODE, 0, s, 0};
				if (fcntl(m_fd, F_PREALLOCATE, &f) < 0)
				{
					ec.assign(errno, get_posix_category());
					return false;
				}
			}
#endif // F_PREALLOCATE

#if defined TORRENT_LINUX || TORRENT_HAS_FALLOCATE
			int ret;
#endif

#if defined TORRENT_LINUX
			ret = my_fallocate(m_fd, 0, 0, s);
			// if we return 0, everything went fine
			// the fallocate call succeeded
			if (ret == 0) return true;
			// otherwise, something went wrong. If the error
			// is ENOSYS, just keep going and do it the old-fashioned
			// way. If fallocate failed with some other error, it
			// probably means the user should know about it, error out
			// and report it.
			if (errno != ENOSYS && errno != EOPNOTSUPP)
			{
				ec.assign(errno, get_posix_category());
				return false;
			}
#endif // TORRENT_LINUX

#if TORRENT_HAS_FALLOCATE
			// if fallocate failed, we have to use posix_fallocate
			// which can be painfully slow
			// if you get a compile error here, you might want to
			// define TORRENT_HAS_FALLOCATE to 0.
			ret = posix_fallocate(m_fd, 0, s);
			// posix_allocate fails with EINVAL in case the underlying
			// filesystem does bot support this operation
			if (ret != 0 && ret != EINVAL)
			{
				ec = error_code(ret, get_posix_category());
				return false;
			}
#endif // TORRENT_HAS_FALLOCATE
		}
#endif // TORRENT_WINDOWS
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
#ifdef TORRENT_MINGW
typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
	LARGE_INTEGER FileOffset;
	LARGE_INTEGER Length;
} FILE_ALLOCATED_RANGE_BUFFER, *PFILE_ALLOCATED_RANGE_BUFFER;
#define FSCTL_QUERY_ALLOCATED_RANGES ((0x9 << 16) | (1 << 14) | (51 << 2) | 3)
#endif
		FILE_ALLOCATED_RANGE_BUFFER buffer;
		DWORD bytes_returned = 0;
		FILE_ALLOCATED_RANGE_BUFFER in;
		error_code ec;
		size_type file_size = get_size(ec);
		if (ec) return start;

		if (m_open_mode & no_buffer)
		{
			boost::uint64_t mask = size_alignment()-1;
			in.FileOffset.QuadPart = start & (~mask);
			in.Length.QuadPart = ((file_size + mask) & ~mask) - in.FileOffset.QuadPart;
			TORRENT_ASSERT((in.Length.QuadPart & mask) == 0);
		}
		else
		{
			in.FileOffset.QuadPart = start;
			in.Length.QuadPart = file_size - start;
		}

		if (!DeviceIoControl(m_file_handle, FSCTL_QUERY_ALLOCATED_RANGES
			, &in, sizeof(FILE_ALLOCATED_RANGE_BUFFER)
			, &buffer, sizeof(FILE_ALLOCATED_RANGE_BUFFER), &bytes_returned, 0))
		{
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
		return start;
#else
		return start;
#endif
	}

}

