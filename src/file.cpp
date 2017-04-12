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

#include "libtorrent/aux_/disable_warnings_push.hpp"

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

// these defines are just in case the system we're on needs them for 64 bit file
// support
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES 1

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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/aux_/max_path.hpp" // for TORRENT_MAX_PATH
#include <cstring>

// for convert_to_wstring and convert_to_native
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/throw.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <sys/stat.h>
#include <climits> // for IOV_MAX

#ifdef TORRENT_WINDOWS
// windows part

#ifndef PtrToPtr64
#define PtrToPtr64(x) (x)
#endif

#include "libtorrent/utf8.hpp"
#include "libtorrent/aux_/win_util.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#ifndef TORRENT_MINGW
#include <direct.h> // for _getcwd, _mkdir
#else
#include <dirent.h>
#endif
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
#define lseek lseek64
#define pread pread64
#define pwrite pwrite64
#define ftruncate ftruncate64
#endif

#elif defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
// mac specifics

#include <copyfile.h>

#endif

// make sure the _FILE_OFFSET_BITS define worked
// on this platform. It's supposed to make file
// related functions support 64-bit offsets.
// this test makes sure lseek() returns a type
// at least 64 bits wide
static_assert(sizeof(lseek(0, 0, 0)) >= 8, "64 bit file operations are required");

#endif // posix part

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if TORRENT_USE_PREADV
# if defined TORRENT_WINDOWS
namespace {

	// wrap the windows function in something that looks
	// like preadv() and pwritev()

	// windows only lets us wait for 64 handles at a time, so this function makes
	// sure we wait for all of them, partially in sequence
	DWORD wait_for_multiple_objects(int num_handles, HANDLE* h)
	{
		int batch_size = (std::min)(num_handles, MAXIMUM_WAIT_OBJECTS);
		while (WaitForMultipleObjects(batch_size, h, TRUE, INFINITE) != WAIT_FAILED)
		{
			h += batch_size;
			num_handles -= batch_size;
			batch_size = (std::min)(num_handles, MAXIMUM_WAIT_OBJECTS);
			if (batch_size <= 0) return WAIT_OBJECT_0;
		}
		return WAIT_FAILED;
	}

	int preadv(HANDLE fd, libtorrent::iovec_t const* bufs, int num_bufs, std::int64_t file_offset)
	{
		TORRENT_ALLOCA(ol, OVERLAPPED, num_bufs);
		std::memset(ol.data(), 0, sizeof(OVERLAPPED) * num_bufs);

		TORRENT_ALLOCA(h, HANDLE, num_bufs);

		for (int i = 0; i < num_bufs; ++i)
		{
			ol[i].OffsetHigh = file_offset >> 32;
			ol[i].Offset = file_offset & 0xffffffff;
			ol[i].hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
			h[i] = ol[i].hEvent;
			if (h[i] == nullptr)
			{
				// we failed to create the event, roll-back and return an error
				for (int j = 0; j < i; ++j) CloseHandle(h[i]);
				return -1;
			}
			file_offset += bufs[i].iov_len;
		}

		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_read;
			if (ReadFile(fd, bufs[i].iov_base, DWORD(bufs[i].iov_len), &num_read, &ol[i]) == FALSE
				&& GetLastError() != ERROR_IO_PENDING
#ifdef ERROR_CANT_WAIT
				&& GetLastError() != ERROR_CANT_WAIT
#endif
				)
			{
				ret = -1;
				goto done;
			}
		}

		if (wait_for_multiple_objects(int(h.size()), h.data()) == WAIT_FAILED)
		{
			ret = -1;
			goto done;
		}

		for (auto& o : ol)
		{
			if (WaitForSingleObject(o.hEvent, INFINITE) == WAIT_FAILED)
			{
				ret = -1;
				break;
			}
			DWORD num_read;
			if (GetOverlappedResult(fd, &o, &num_read, FALSE) == FALSE)
			{
#ifdef ERROR_CANT_WAIT
				TORRENT_ASSERT(GetLastError() != ERROR_CANT_WAIT);
#endif
				ret = -1;
				break;
			}
			ret += num_read;
		}
done:

		for (auto hnd : h)
			CloseHandle(hnd);

		return ret;
	}

	int pwritev(HANDLE fd, libtorrent::iovec_t const* bufs, int num_bufs, std::int64_t file_offset)
	{
		TORRENT_ALLOCA(ol, OVERLAPPED, num_bufs);
		std::memset(ol.data(), 0, sizeof(OVERLAPPED) * num_bufs);

		TORRENT_ALLOCA(h, HANDLE, num_bufs);

		for (int i = 0; i < num_bufs; ++i)
		{
			ol[i].OffsetHigh = file_offset >> 32;
			ol[i].Offset = file_offset & 0xffffffff;
			ol[i].hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
			h[i] = ol[i].hEvent;
			if (h[i] == nullptr)
			{
				// we failed to create the event, roll-back and return an error
				for (int j = 0; j < i; ++j) CloseHandle(h[i]);
				return -1;
			}
			file_offset += bufs[i].iov_len;
		}

		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_written;
			if (WriteFile(fd, bufs[i].iov_base, DWORD(bufs[i].iov_len), &num_written, &ol[i]) == FALSE
				&& GetLastError() != ERROR_IO_PENDING
#ifdef ERROR_CANT_WAIT
				&& GetLastError() != ERROR_CANT_WAIT
#endif
				)
			{
				ret = -1;
				goto done;
			}
		}

		if (wait_for_multiple_objects(int(h.size()), h.data()) == WAIT_FAILED)
		{
			ret = -1;
			goto done;
		}

		for (auto& o : ol)
		{
			if (WaitForSingleObject(o.hEvent, INFINITE) == WAIT_FAILED)
			{
				ret = -1;
				break;
			}
			DWORD num_written;
			if (GetOverlappedResult(fd, &o, &num_written, FALSE) == FALSE)
			{
#ifdef ERROR_CANT_WAIT
				TORRENT_ASSERT(GetLastError() != ERROR_CANT_WAIT);
#endif
				ret = -1;
				break;
			}
			ret += num_written;
		}
done:

		for (auto hnd : h)
			CloseHandle(hnd);

		return ret;
	}
}
# else
#  undef _BSD_SOURCE
#  define _BSD_SOURCE // deprecated since glibc 2.20
#  undef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE
#  include <sys/uio.h>
# endif
#endif

static_assert((libtorrent::file::rw_mask & libtorrent::file::sparse) == 0, "internal flags error");
static_assert((libtorrent::file::rw_mask & libtorrent::file::attribute_mask) == 0, "internal flags error");
static_assert((libtorrent::file::sparse & libtorrent::file::attribute_mask) == 0, "internal flags error");

#if defined TORRENT_WINDOWS && defined UNICODE && !TORRENT_USE_WSTRING

#ifdef _MSC_VER
#pragma message ( "wide character support not available. Files will be saved using narrow string names" )
#else
#warning "wide character support not available. Files will be saved using narrow string names"
#endif

#endif // TORRENT_WINDOWS

namespace libtorrent {

	template <typename T>
	std::unique_ptr<T, decltype(&std::free)> make_free_holder(T* ptr)
	{
		return std::unique_ptr<T, decltype(&std::free)>(ptr, &std::free);
	}

	directory::directory(std::string const& path, error_code& ec)
		: m_done(false)
	{
		ec.clear();
		std::string p{ path };

#ifdef TORRENT_WINDOWS
		// the path passed to FindFirstFile() must be
		// a pattern
		p.append((!p.empty() && p.back() != '\\') ? "\\*" : "*");
#else
		// the path passed to opendir() may not
		// end with a /
		if (!p.empty() && p.back() == '/')
			p.pop_back();
#endif

		native_path_string f = convert_to_native_path_string(p);

#ifdef TORRENT_WINDOWS
		m_inode = 0;

#if TORRENT_USE_WSTRING
#define FindFirstFile_ FindFirstFileW
#else
#define FindFirstFile_ FindFirstFileA
#endif
		m_handle = FindFirstFile_(f.c_str(), &m_fd);
		if (m_handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), system_category());
			m_done = true;
			return;
		}
#undef FindFirstFile_
#else

		std::memset(&m_dirent, 0, sizeof(dirent));
		m_name[0] = 0;

		m_handle = ::opendir(f.c_str());
		if (m_handle == nullptr)
		{
			ec.assign(errno, system_category());
			m_done = true;
			return;
		}
		// read the first entry
		next(ec);
#endif
	}

	directory::~directory()
	{
#ifdef TORRENT_WINDOWS
		if (m_handle != INVALID_HANDLE_VALUE)
			FindClose(m_handle);
#else
		if (m_handle) closedir(m_handle);
#endif
	}

	std::uint64_t directory::inode() const
	{
#ifdef TORRENT_WINDOWS
		return m_inode;
#else
		return m_dirent.d_ino;
#endif
	}

	std::string directory::file() const
	{
#ifdef TORRENT_WINDOWS
		return convert_from_native_path(m_fd.cFileName);
#else
		return convert_from_native(m_dirent.d_name);
#endif
	}

	void directory::next(error_code& ec)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
#if TORRENT_USE_WSTRING
#define FindNextFile_ FindNextFileW
#else
#define FindNextFile_ FindNextFileA
#endif
		if (FindNextFile_(m_handle, &m_fd) == 0)
		{
			m_done = true;
			int err = GetLastError();
			if (err != ERROR_NO_MORE_FILES)
				ec.assign(err, system_category());
		}
#undef FindNextFile_
		++m_inode;
#else
		dirent* dummy;
		if (readdir_r(m_handle, &m_dirent, &dummy) != 0)
		{
			ec.assign(errno, system_category());
			m_done = true;
		}
		if (dummy == nullptr) m_done = true;
#endif
	}

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (-1)
#endif

#ifdef TORRENT_WINDOWS
	struct overlapped_t
	{
		overlapped_t()
		{
			std::memset(&ol, 0, sizeof(ol));
			ol.hEvent = CreateEvent(0, true, false, 0);
		}
		~overlapped_t()
		{
			if (ol.hEvent != INVALID_HANDLE_VALUE)
				CloseHandle(ol.hEvent);
		}
		int wait(HANDLE file, error_code& ec)
		{
			if (ol.hEvent != INVALID_HANDLE_VALUE
				&& WaitForSingleObject(ol.hEvent, INFINITE) == WAIT_FAILED)
			{
				ec.assign(GetLastError(), system_category());
				return -1;
			}

			DWORD ret;
			if (GetOverlappedResult(file, &ol, &ret, false) == 0)
			{
				DWORD last_error = GetLastError();
				if (last_error != ERROR_HANDLE_EOF)
				{
#ifdef ERROR_CANT_WAIT
					TORRENT_ASSERT(last_error != ERROR_CANT_WAIT);
#endif
					ec.assign(last_error, system_category());
					return -1;
				}
			}
			return ret;
		}

		OVERLAPPED ol;
	};
#endif // TORRENT_WINDOWS


#ifdef TORRENT_WINDOWS
	bool get_manage_volume_privs();

	// this needs to be run before CreateFile
	bool file::has_manage_volume_privs = get_manage_volume_privs();
#endif

	file::file()
		: m_file_handle(INVALID_HANDLE_VALUE)
		, m_open_mode(0)
	{}

	file::file(std::string const& path, std::uint32_t const mode, error_code& ec)
		: m_file_handle(INVALID_HANDLE_VALUE)
		, m_open_mode(0)
	{
		// the return value is not important, since the
		// error code contains the same information
		open(path, mode, ec);
	}

	file::~file()
	{
		close();
	}

	bool file::open(std::string const& path, std::uint32_t mode, error_code& ec)
	{
		close();
		native_path_string file_path = convert_to_native_path_string(path);

#ifdef TORRENT_WINDOWS

		struct open_mode_t
		{
			DWORD rw_mode;
			DWORD create_mode;
		};

		static const open_mode_t mode_array[] =
		{
			// read_only
			{GENERIC_READ, OPEN_EXISTING},
			// write_only
			{GENERIC_WRITE, OPEN_ALWAYS},
			// read_write
			{GENERIC_WRITE | GENERIC_READ, OPEN_ALWAYS},
		};

		static const DWORD attrib_array[] =
		{
			FILE_ATTRIBUTE_NORMAL, // no attrib
			FILE_ATTRIBUTE_HIDDEN, // hidden
			FILE_ATTRIBUTE_NORMAL, // executable
			FILE_ATTRIBUTE_HIDDEN, // hidden + executable
		};

#if TORRENT_USE_WSTRING
#define CreateFile_ CreateFileW
#else
#define CreateFile_ CreateFileA
#endif

		TORRENT_ASSERT((mode & rw_mask) < sizeof(mode_array)/sizeof(mode_array[0]));
		open_mode_t const& m = mode_array[mode & rw_mask];
		DWORD a = attrib_array[(mode & attribute_mask) >> 12];

		// one might think it's a good idea to pass in FILE_FLAG_RANDOM_ACCESS. It
		// turns out that it isn't. That flag will break your operating system:
		// http://support.microsoft.com/kb/2549369

		DWORD flags = ((mode & random_access) ? 0 : FILE_FLAG_SEQUENTIAL_SCAN)
			| (a ? a : FILE_ATTRIBUTE_NORMAL)
			| FILE_FLAG_OVERLAPPED
			| ((mode & no_cache) ? FILE_FLAG_WRITE_THROUGH : 0);

		handle_type handle = CreateFile_(file_path.c_str(), m.rw_mode
			, (mode & lock_file) ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE
			, 0, m.create_mode, flags, 0);

#undef CreateFile_

		if (handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), system_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		m_file_handle = handle;

		// try to make the file sparse if supported
		// only set this flag if the file is opened for writing
		if ((mode & file::sparse) && (mode & rw_mask) != read_only)
		{
			DWORD temp;
			overlapped_t ol;
			BOOL ret = ::DeviceIoControl(native_handle(), FSCTL_SET_SPARSE, 0, 0
				, 0, 0, &temp, &ol.ol);
			error_code error;
			if (ret == FALSE && GetLastError() == ERROR_IO_PENDING)
				ol.wait(native_handle(), error);
		}
#else // TORRENT_WINDOWS

		// rely on default umask to filter x and w permissions
		// for group and others
		int permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		if (mode & attribute_executable)
			permissions |= S_IXGRP | S_IXOTH | S_IXUSR;
#ifdef O_BINARY
		static const int mode_array[] = {O_RDONLY | O_BINARY, O_WRONLY | O_CREAT | O_BINARY, O_RDWR | O_CREAT | O_BINARY};
#else
		static const int mode_array[] = {O_RDONLY, O_WRONLY | O_CREAT, O_RDWR | O_CREAT};
#endif

		int open_mode = 0
#ifdef O_NOATIME
			| ((mode & no_atime) ? O_NOATIME : 0)
#endif
#ifdef O_SYNC
			| ((mode & no_cache) ? O_SYNC : 0)
#endif
			;

		handle_type handle = ::open(file_path.c_str()
			, mode_array[mode & rw_mask] | open_mode
			, permissions);

#ifdef O_NOATIME
		// O_NOATIME is not allowed for files we don't own
		// so, if we get EPERM when we try to open with it
		// try again without O_NOATIME
		if (handle == -1 && (mode & no_atime) && errno == EPERM)
		{
			mode &= ~no_atime;
			open_mode &= ~O_NOATIME;
			handle = ::open(file_path.c_str(), mode_array[mode & rw_mask] | open_mode
				, permissions);
		}
#endif
		if (handle == -1)
		{
			ec.assign(errno, system_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		m_file_handle = handle;

		// The purpose of the lock_file flag is primarily to prevent other
		// processes from corrupting files that are being used by libtorrent.
		// the posix file locking mechanism does not prevent others from
		// accessing files, unless they also attempt to lock the file. That's
		// why the SETLK mechanism is not used here.

#ifdef DIRECTIO_ON
		// for solaris
		if (mode & no_cache)
		{
			int yes = 1;
			directio(native_handle(), DIRECTIO_ON);
		}
#endif

#ifdef F_NOCACHE
		// for BSD/Mac
		if (mode & no_cache)
		{
			int yes = 1;
			fcntl(native_handle(), F_NOCACHE, &yes);

#ifdef F_NODIRECT
			// it's OK to temporarily cache written pages
			fcntl(native_handle(), F_NODIRECT, &yes);
#endif
		}
#endif

#ifdef POSIX_FADV_RANDOM
		if (mode & random_access)
		{
			// disable read-ahead
			posix_fadvise(native_handle(), 0, 0, POSIX_FADV_RANDOM);
		}
#endif

#endif
		m_open_mode = mode;

		TORRENT_ASSERT(is_open());
		return true;
	}

	bool file::is_open() const
	{
		return m_file_handle != INVALID_HANDLE_VALUE;
	}

#ifdef TORRENT_WINDOWS
	// returns true if the given file has any regions that are
	// sparse, i.e. not allocated.
	bool is_sparse(HANDLE file)
	{
		LARGE_INTEGER file_size;
		if (!GetFileSizeEx(file, &file_size))
			return false;

		overlapped_t ol;
		if (ol.ol.hEvent == nullptr) return false;

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
			, out, sizeof(out), &returned_bytes, &ol.ol);

		if (ret == FALSE && GetLastError() == ERROR_IO_PENDING)
		{
			error_code ec;
			returned_bytes = ol.wait(file, ec);
			if (ec) return true;
		}
		else if (ret == FALSE)
		{
			return true;
		}

		// if we have more than one range in the file, we're sparse
		if (returned_bytes != sizeof(FILE_ALLOCATED_RANGE_BUFFER)) {
			return true;
		}

		return (in.Length.QuadPart != out[0].Length.QuadPart);
	}
#endif

	void file::close()
	{
		if (!is_open()) return;

#ifdef TORRENT_WINDOWS

		// if this file is open for writing, has the sparse
		// flag set, but there are no sparse regions, unset
		// the flag
		std::uint32_t rw_mode = m_open_mode & rw_mask;
		if ((rw_mode != read_only)
			&& (m_open_mode & sparse)
			&& !is_sparse(native_handle()))
		{
			overlapped_t ol;
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
			BOOL ret = ::DeviceIoControl(native_handle(), FSCTL_SET_SPARSE, &b, sizeof(b)
				, 0, 0, &temp, &ol.ol);
			error_code ec;
			if (ret == FALSE && GetLastError() == ERROR_IO_PENDING)
			{
				ol.wait(native_handle(), ec);
			}
		}

		CloseHandle(native_handle());
#else
		if (m_file_handle != INVALID_HANDLE_VALUE)
			::close(m_file_handle);
#endif

		m_file_handle = INVALID_HANDLE_VALUE;

		m_open_mode = 0;
	}

	namespace {

#if !TORRENT_USE_PREADV
	void gather_copy(span<iovec_t const> bufs, char* dst)
	{
		std::size_t offset = 0;
		for (auto buf : bufs)
		{
			std::memcpy(dst + offset, buf.iov_base, buf.iov_len);
			offset += buf.iov_len;
		}
	}

	void scatter_copy(span<iovec_t const> bufs, char const* src)
	{
		std::size_t offset = 0;
		for (auto buf : bufs)
		{
			std::memcpy(buf.iov_base, src + offset, buf.iov_len);
			offset += buf.iov_len;
		}
	}

	bool coalesce_read_buffers(span<iovec_t const>& bufs
		, iovec_t& tmp)
	{
		std::size_t const buf_size = aux::numeric_cast<std::size_t>(bufs_size(bufs));
		char* buf = static_cast<char*>(std::malloc(buf_size));
		if (!buf) return false;
		tmp.iov_base = buf;
		tmp.iov_len = buf_size;
		bufs = span<iovec_t const>(tmp);
		return true;
	}

	void coalesce_read_buffers_end(span<iovec_t const> bufs
		, char* const buf, bool const copy)
	{
		if (copy) scatter_copy(bufs, buf);
		std::free(buf);
	}

	bool coalesce_write_buffers(span<iovec_t const>& bufs
		, iovec_t& tmp)
	{
		std::size_t const buf_size = aux::numeric_cast<std::size_t>(bufs_size(bufs));
		char* buf = static_cast<char*>(std::malloc(buf_size));
		if (!buf) return false;
		gather_copy(bufs, buf);
		tmp.iov_base = buf;
		tmp.iov_len = buf_size;
		bufs = span<iovec_t const>(tmp);
		return true;
	}
#endif // TORRENT_USE_PREADV

	template <class Fun>
	std::int64_t iov(Fun f, handle_type fd, std::int64_t file_offset
		, span<iovec_t const> bufs, error_code& ec)
	{
#if TORRENT_USE_PREADV

		int ret = 0;
		while (!bufs.empty())
		{
#ifdef IOV_MAX
			auto const nbufs = bufs.first((std::min)(int(bufs.size()), IOV_MAX));
#else
			auto const nbufs = bufs;
#endif

			int tmp_ret = 0;
			tmp_ret = f(fd, nbufs.data(), int(nbufs.size()), file_offset);
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

			// we got a short read/write. It's either 0, and we're at EOF, or we
			// just need to issue the read/write operation again. In either case,
			// punt that to the upper layer, as reissuing the operations is
			// complicated here
			const int expected_len = bufs_size(nbufs);
			if (tmp_ret < expected_len) break;

			bufs = bufs.subspan(nbufs.size());
		}
		return ret;

#elif TORRENT_USE_PREAD

		std::int64_t ret = 0;
		for (auto i : bufs)
		{
			std::int64_t const tmp_ret = f(fd, i.iov_base, i.iov_len, file_offset);
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
			if (tmp_ret < int(i.iov_len)) break;
		}

		return ret;

#else // not PREADV nor PREAD

		int ret = 0;

#ifdef TORRENT_WINDOWS
		if (SetFilePointerEx(fd, offs, &offs, FILE_BEGIN) == FALSE)
		{
			ec.assign(GetLastError(), system_category());
			return -1;
		}
#else
		if (lseek(fd, file_offset, SEEK_SET) < 0)
		{
			ec.assign(errno, system_category());
			return -1;
		}
#endif

		for (auto i : bufs)
		{
			int tmp_ret = f(fd, i.iov_base, i.iov_len);
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
			if (tmp_ret < int(i.iov_len)) break;
		}

		return ret;

#endif // USE_PREADV
	}

	} // anonymous namespace

	// this has to be thread safe and atomic. i.e. on posix systems it has to be
	// turned into a series of pread() calls
	std::int64_t file::readv(std::int64_t file_offset, span<iovec_t const> bufs
		, error_code& ec, std::uint32_t flags)
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
		TORRENT_ASSERT((m_open_mode & rw_mask) == read_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(!bufs.empty());
		TORRENT_ASSERT(is_open());

#if TORRENT_USE_PREADV
		TORRENT_UNUSED(flags);

		std::int64_t ret = iov(&::preadv, native_handle(), file_offset, bufs, ec);
#else

		// there's no point in coalescing single buffer writes
		if (bufs.size() == 1)
		{
			flags &= ~file::coalesce_buffers;
		}

		iovec_t tmp;
		span<iovec_t const> tmp_bufs = bufs;
		if ((flags & file::coalesce_buffers))
		{
			if (!coalesce_read_buffers(tmp_bufs, tmp))
				// ok, that failed, don't coalesce this read
				flags &= ~file::coalesce_buffers;
		}

#if TORRENT_USE_PREAD
		std::int64_t ret = iov(&::pread, native_handle(), file_offset, tmp_bufs, ec);
#else
		std::int64_t ret = iov(&::read, native_handle(), file_offset, tmp_bufs, ec);
#endif

		if ((flags & file::coalesce_buffers))
			coalesce_read_buffers_end(bufs
				, static_cast<char*>(tmp.iov_base), !ec);

#endif
		return ret;
	}

	// This has to be thread safe, i.e. atomic.
	// that means, on posix this has to be turned into a series of
	// pwrite() calls
	std::int64_t file::writev(std::int64_t file_offset, span<iovec_t const> bufs
		, error_code& ec, std::uint32_t flags)
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
		TORRENT_ASSERT((m_open_mode & rw_mask) == write_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(!bufs.empty());
		TORRENT_ASSERT(is_open());

		ec.clear();

#if TORRENT_USE_PREADV
		TORRENT_UNUSED(flags);

		std::int64_t ret = iov(&::pwritev, native_handle(), file_offset, bufs, ec);
#else

		// there's no point in coalescing single buffer writes
		if (bufs.size() == 1)
		{
			flags &= ~file::coalesce_buffers;
		}

		iovec_t tmp;
		if (flags & file::coalesce_buffers)
		{
			if (!coalesce_write_buffers(bufs, tmp))
				// ok, that failed, don't coalesce writes
				flags &= ~file::coalesce_buffers;
		}

#if TORRENT_USE_PREAD
		std::int64_t ret = iov(&::pwrite, native_handle(), file_offset, bufs, ec);
#else
		std::int64_t ret = iov(&::write, native_handle(), file_offset, bufs, ec);
#endif

		if (flags & file::coalesce_buffers)
			std::free(tmp.iov_base);

#endif
#if TORRENT_USE_FDATASYNC \
	&& !defined F_NOCACHE && \
	!defined DIRECTIO_ON
		if (m_open_mode & no_cache)
		{
			if (fdatasync(native_handle()) != 0
				&& errno != EINVAL
				&& errno != ENOSYS)
			{
				ec.assign(errno, system_category());
			}
		}
#endif
		return ret;
	}

#ifdef TORRENT_WINDOWS
	bool get_manage_volume_privs()
	{
		typedef BOOL (WINAPI *OpenProcessToken_t)(
			HANDLE ProcessHandle,
			DWORD DesiredAccess,
			PHANDLE TokenHandle);

		typedef BOOL (WINAPI *LookupPrivilegeValue_t)(
			LPCSTR lpSystemName,
			LPCSTR lpName,
			PLUID lpLuid);

		typedef BOOL (WINAPI *AdjustTokenPrivileges_t)(
			HANDLE TokenHandle,
			BOOL DisableAllPrivileges,
			PTOKEN_PRIVILEGES NewState,
			DWORD BufferLength,
			PTOKEN_PRIVILEGES PreviousState,
			PDWORD ReturnLength);

		auto OpenProcessToken =
			aux::get_library_procedure<aux::advapi32, OpenProcessToken_t>("OpenProcessToken");
		auto LookupPrivilegeValue =
			aux::get_library_procedure<aux::advapi32, LookupPrivilegeValue_t>("LookupPrivilegeValueA");
		auto AdjustTokenPrivileges =
			aux::get_library_procedure<aux::advapi32, AdjustTokenPrivileges_t>("AdjustTokenPrivileges");

		if (OpenProcessToken == nullptr || LookupPrivilegeValue == nullptr || AdjustTokenPrivileges == nullptr) return false;


		HANDLE token;
		if (!OpenProcessToken(GetCurrentProcess()
			, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
			return false;

		TOKEN_PRIVILEGES privs;
		if (!LookupPrivilegeValue(nullptr, "SeManageVolumePrivilege"
			, &privs.Privileges[0].Luid))
		{
			CloseHandle(token);
			return false;
		}

		privs.PrivilegeCount = 1;
		privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		bool ret = AdjustTokenPrivileges(token, FALSE, &privs, 0, nullptr, nullptr)
			&& GetLastError() == ERROR_SUCCESS;

		CloseHandle(token);

		return ret;
	}

	void set_file_valid_data(HANDLE f, std::int64_t size)
	{
		typedef BOOL (WINAPI *SetFileValidData_t)(HANDLE, LONGLONG);
		auto SetFileValidData =
			aux::get_library_procedure<aux::kernel32, SetFileValidData_t>("SetFileValidData");

		if (SetFileValidData == nullptr) return;

		// we don't necessarily expect to have enough
		// privilege to do this, so ignore errors.
		SetFileValidData(f, size);
	}
#endif

	bool file::set_size(std::int64_t s, error_code& ec)
	{
		TORRENT_ASSERT(is_open());
		TORRENT_ASSERT(s >= 0);

#ifdef TORRENT_WINDOWS

		LARGE_INTEGER offs;
		LARGE_INTEGER cur_size;
		if (GetFileSizeEx(native_handle(), &cur_size) == FALSE)
		{
			ec.assign(GetLastError(), system_category());
			return false;
		}
		offs.QuadPart = s;
		// only set the file size if it's not already at
		// the right size. We don't want to update the
		// modification time if we don't have to
		if (cur_size.QuadPart != s)
		{
			if (SetFilePointerEx(native_handle(), offs, &offs, FILE_BEGIN) == FALSE)
			{
				ec.assign(GetLastError(), system_category());
				return false;
			}
			if (::SetEndOfFile(native_handle()) == FALSE)
			{
				ec.assign(GetLastError(), system_category());
				return false;
			}
		}

#if _WIN32_WINNT >= 0x0600 // only if Windows Vista or newer
		if ((m_open_mode & sparse) == 0)
		{
			typedef DWORD (WINAPI *GetFileInformationByHandleEx_t)(HANDLE hFile
				, FILE_INFO_BY_HANDLE_CLASS FileInformationClass
				, LPVOID lpFileInformation
				, DWORD dwBufferSize);

			auto GetFileInformationByHandleEx =
				aux::get_library_procedure<aux::kernel32, GetFileInformationByHandleEx_t>("GetFileInformationByHandleEx");

			offs.QuadPart = 0;
			if (GetFileInformationByHandleEx != nullptr)
			{
				// only allocate the space if the file
				// is not fully allocated
				FILE_STANDARD_INFO inf;
				if (GetFileInformationByHandleEx(native_handle()
					, FileStandardInfo, &inf, sizeof(inf)) == FALSE)
				{
					ec.assign(GetLastError(), system_category());
					if (ec) return false;
				}
				offs = inf.AllocationSize;
			}

			if (offs.QuadPart != s)
			{
				// if the user has permissions, avoid filling
				// the file with zeroes, but just fill it with
				// garbage instead
				set_file_valid_data(m_file_handle, s);
			}
		}
#endif // if Windows Vista
#else // NON-WINDOWS
		struct stat st;
		if (fstat(native_handle(), &st) != 0)
		{
			ec.assign(errno, system_category());
			return false;
		}

		// only truncate the file if it doesn't already
		// have the right size. We don't want to update
		if (st.st_size != s && ftruncate(native_handle(), s) < 0)
		{
			ec.assign(errno, system_category());
			return false;
		}

		// if we're not in sparse mode, allocate the storage
		// but only if the number of allocated blocks for the file
		// is less than the file size. Otherwise we would just
		// update the modification time of the file for no good
		// reason.
		if ((m_open_mode & sparse) == 0
			&& std::int64_t(st.st_blocks) < (s + st.st_blksize - 1) / st.st_blksize)
		{
			// How do we know that the file is already allocated?
			// if we always try to allocate the space, we'll update
			// the modification time without actually changing the file
			// but if we don't do anything if the file size is
#ifdef F_PREALLOCATE
			fstore_t f = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, s, 0};
			if (fcntl(native_handle(), F_PREALLOCATE, &f) < 0)
			{
				if (errno != ENOSPC)
				{
					ec.assign(errno, system_category());
					return false;
				}
				// ok, let's try to allocate non contiguous space then
				f.fst_flags = F_ALLOCATEALL;
				if (fcntl(native_handle(), F_PREALLOCATE, &f) < 0)
				{
					ec.assign(errno, system_category());
					return false;
				}
			}
#endif // F_PREALLOCATE

#ifdef F_ALLOCSP64
			flock64 fl64;
			fl64.l_whence = SEEK_SET;
			fl64.l_start = 0;
			fl64.l_len = s;
			if (fcntl(native_handle(), F_ALLOCSP64, &fl64) < 0)
			{
				ec.assign(errno, system_category());
				return false;
			}

#endif // F_ALLOCSP64

#if TORRENT_HAS_FALLOCATE
			// if fallocate failed, we have to use posix_fallocate
			// which can be painfully slow
			// if you get a compile error here, you might want to
			// define TORRENT_HAS_FALLOCATE to 0.
			int const ret = posix_fallocate(native_handle(), 0, s);
			// posix_allocate fails with EINVAL in case the underlying
			// filesystem does not support this operation
			if (ret != 0 && ret != EINVAL)
			{
				ec.assign(ret, system_category());
				return false;
			}
#endif // TORRENT_HAS_FALLOCATE
		}
#endif // TORRENT_WINDOWS
		return true;
	}

	std::int64_t file::get_size(error_code& ec) const
	{
#ifdef TORRENT_WINDOWS
		LARGE_INTEGER file_size;
		if (!GetFileSizeEx(native_handle(), &file_size))
		{
			ec.assign(GetLastError(), system_category());
			return -1;
		}
		return file_size.QuadPart;
#else
		struct stat fs;
		if (fstat(native_handle(), &fs) != 0)
		{
			ec.assign(errno, system_category());
			return -1;
		}
		return fs.st_size;
#endif
	}

	std::int64_t file::sparse_end(std::int64_t start) const
	{
#ifdef TORRENT_WINDOWS

#ifndef FSCTL_QUERY_ALLOCATED_RANGES
typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
	LARGE_INTEGER FileOffset;
	LARGE_INTEGER Length;
} FILE_ALLOCATED_RANGE_BUFFER;
#define FSCTL_QUERY_ALLOCATED_RANGES ((0x9 << 16) | (1 << 14) | (51 << 2) | 3)
#endif // TORRENT_MINGW

		FILE_ALLOCATED_RANGE_BUFFER buffer;
		DWORD bytes_returned = 0;
		FILE_ALLOCATED_RANGE_BUFFER in;
		error_code ec;
		std::int64_t file_size = get_size(ec);
		if (ec) return start;

		in.FileOffset.QuadPart = start;
		in.Length.QuadPart = file_size - start;

		if (!DeviceIoControl(native_handle(), FSCTL_QUERY_ALLOCATED_RANGES
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
		std::int64_t ret = lseek(native_handle(), start, SEEK_DATA);
		if (ret < 0) return start;
		return start;
#else
		return start;
#endif
	}
}
