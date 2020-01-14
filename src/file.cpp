/*

Copyright (c) 2003-2018, Arvid Norberg
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

// these defines are just in case the system we're on needs them for 64 bit file
// support
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES 1

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

#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/aux_/path.hpp" // for convert_to_native_path_string
#include "libtorrent/string_util.hpp"
#include <cstring>

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
		int batch_size = std::min(num_handles, MAXIMUM_WAIT_OBJECTS);
		while (WaitForMultipleObjects(batch_size, h, TRUE, INFINITE) != WAIT_FAILED)
		{
			h += batch_size;
			num_handles -= batch_size;
			batch_size = std::min(num_handles, MAXIMUM_WAIT_OBJECTS);
			if (batch_size <= 0) return WAIT_OBJECT_0;
		}
		return WAIT_FAILED;
	}

	int allocate_overlapped(::iovec const* bufs, lt::span<OVERLAPPED> ol
		, lt::span<HANDLE> h, std::int64_t file_offset)
	{
		std::memset(ol.data(), 0, sizeof(OVERLAPPED) * ol.size());
		for (std::ptrdiff_t i = 0; i < ol.size(); ++i)
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
		return 0;
	}

	int preadv(HANDLE fd, ::iovec const* bufs, int num_bufs, std::int64_t const file_offset)
	{
		TORRENT_ALLOCA(ol, OVERLAPPED, num_bufs);
		TORRENT_ALLOCA(h, HANDLE, num_bufs);

		if (allocate_overlapped(bufs, ol, h, file_offset) < 0) return -1;

		BOOST_SCOPE_EXIT_ALL(&h) {
			for (auto hnd : h)
				CloseHandle(hnd);
		};

		int num_waits = num_bufs;
		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_read;
			if (ReadFile(fd, bufs[i].iov_base, DWORD(bufs[i].iov_len), &num_read, &ol[i]) == FALSE)
			{
				DWORD const last_error = GetLastError();
				if (last_error == ERROR_HANDLE_EOF)
				{
					num_waits = i;
					break;
				}
				else if (last_error != ERROR_IO_PENDING
#ifdef ERROR_CANT_WAIT
					&& last_error != ERROR_CANT_WAIT
#endif
					)
				{
					return -1;
				}
			}
		}

		if (num_waits == 0) return 0;

		if (wait_for_multiple_objects(num_waits, h.data()) == WAIT_FAILED)
			return -1;

		int ret = 0;
		for (auto& o : ol.first(num_waits))
		{
			if (WaitForSingleObject(o.hEvent, INFINITE) == WAIT_FAILED)
				return -1;

			DWORD num_read;
			if (GetOverlappedResult(fd, &o, &num_read, FALSE) == FALSE)
			{
				DWORD const last_error = GetLastError();
				if (last_error != ERROR_HANDLE_EOF)
				{
#ifdef ERROR_CANT_WAIT
					TORRENT_ASSERT(last_error != ERROR_CANT_WAIT);
#endif
					return -1;
				}
			}
			ret += num_read;
		}

		return ret;
	}

	int pwritev(HANDLE fd, ::iovec const* bufs, int num_bufs, std::int64_t const file_offset)
	{
		TORRENT_ALLOCA(ol, OVERLAPPED, num_bufs);
		TORRENT_ALLOCA(h, HANDLE, num_bufs);

		if (allocate_overlapped(bufs, ol, h, file_offset) < 0) return -1;

		BOOST_SCOPE_EXIT_ALL(&h) {
			for (auto hnd : h)
				CloseHandle(hnd);
		};

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
				return -1;
			}
		}

		if (wait_for_multiple_objects(int(h.size()), h.data()) == WAIT_FAILED)
			return -1;

		int ret = 0;
		for (auto& o : ol)
		{
			if (WaitForSingleObject(o.hEvent, INFINITE) == WAIT_FAILED)
				return -1;

			DWORD num_written;
			if (GetOverlappedResult(fd, &o, &num_written, FALSE) == FALSE)
			{
#ifdef ERROR_CANT_WAIT
				TORRENT_ASSERT(GetLastError() != ERROR_CANT_WAIT);
#endif
				return -1;
			}
			ret += num_written;
		}

		return ret;
	}
} // namespace
# else

#  ifdef __clang__
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wreserved-id-macro"
#   pragma clang diagnostic ignored "-Wunused-macros"
#  endif

#  undef _BSD_SOURCE
#  define _BSD_SOURCE // deprecated since glibc 2.20
#  undef _DEFAULT_SOURCE
#  define _DEFAULT_SOURCE
#  include <sys/uio.h>

#  ifdef __clang__
#   pragma clang diagnostic pop
#  endif

# endif
#endif

namespace libtorrent {

static_assert(!(open_mode::rw_mask & open_mode::sparse), "internal flags error");
static_assert(!(open_mode::rw_mask & open_mode::attribute_mask), "internal flags error");
static_assert(!(open_mode::sparse & open_mode::attribute_mask), "internal flags error");

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
		m_handle = FindFirstFileW(f.c_str(), &m_fd);
		if (m_handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), system_category());
			m_done = true;
			return;
		}
#else
		m_handle = ::opendir(f.c_str());
		if (m_handle == nullptr)
		{
			ec.assign(errno, system_category());
			m_done = true;
			return;
		}
		// read the first entry
		next(ec);
#endif // TORRENT_WINDOWS
	}

	directory::~directory()
	{
#ifdef TORRENT_WINDOWS
		if (m_handle != INVALID_HANDLE_VALUE)
			FindClose(m_handle);
#else
		if (m_handle) ::closedir(m_handle);
#endif
	}

	std::string directory::file() const
	{
#ifdef TORRENT_WINDOWS
		return convert_from_native_path(m_fd.cFileName);
#else
		return convert_from_native_path(m_name.c_str());
#endif
	}

	void directory::next(error_code& ec)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
		if (FindNextFileW(m_handle, &m_fd) == 0)
		{
			m_done = true;
			int err = GetLastError();
			if (err != ERROR_NO_MORE_FILES)
				ec.assign(err, system_category());
		}
#else
		struct dirent* de;
		errno = 0;
		if ((de = ::readdir(m_handle)) != nullptr)
		{
			m_name = de->d_name;
		}
		else
		{
			if (errno) ec.assign(errno, system_category());
			m_done = true;
		}
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
	void acquire_manage_volume_privs();
#endif

	file::file() : m_file_handle(INVALID_HANDLE_VALUE)
	{}

	file::file(std::string const& path, open_mode_t const mode, error_code& ec)
		: m_file_handle(INVALID_HANDLE_VALUE)
	{
		// the return value is not important, since the
		// error code contains the same information
		open(path, mode, ec);
	}

	file::~file()
	{
		close();
	}

	bool file::open(std::string const& path, open_mode_t mode, error_code& ec)
	{
		close();
		native_path_string file_path = convert_to_native_path_string(path);

#ifdef TORRENT_WINDOWS

		struct win_open_mode_t
		{
			DWORD rw_mode;
			DWORD create_mode;
		};

		static std::array<win_open_mode_t, 3> const mode_array{
		{
			// read_only
			{GENERIC_READ, OPEN_EXISTING},
			// write_only
			{GENERIC_WRITE, OPEN_ALWAYS},
			// read_write
			{GENERIC_WRITE | GENERIC_READ, OPEN_ALWAYS},
		}};

		static std::array<DWORD, 4> const attrib_array{
		{
			FILE_ATTRIBUTE_NORMAL, // no attrib
			FILE_ATTRIBUTE_HIDDEN, // hidden
			FILE_ATTRIBUTE_NORMAL, // executable
			FILE_ATTRIBUTE_HIDDEN, // hidden + executable
		}};

		TORRENT_ASSERT(static_cast<std::uint32_t>(mode & open_mode::rw_mask) < mode_array.size());
		win_open_mode_t const& m = mode_array[static_cast<std::uint32_t>(mode & open_mode::rw_mask)];
		DWORD a = attrib_array[static_cast<std::uint32_t>(mode & open_mode::attribute_mask) >> 7];

		// one might think it's a good idea to pass in FILE_FLAG_RANDOM_ACCESS. It
		// turns out that it isn't. That flag will break your operating system:
		// http://support.microsoft.com/kb/2549369

		DWORD const flags = ((mode & open_mode::random_access) ? 0 : FILE_FLAG_SEQUENTIAL_SCAN)
			| a
			| FILE_FLAG_OVERLAPPED
			| ((mode & open_mode::no_cache) ? FILE_FLAG_WRITE_THROUGH : 0);

		if (!(mode & open_mode::sparse))
		{
			// Enable privilege required by SetFileValidData()
			// https://docs.microsoft.com/en-us/windows/desktop/api/fileapi/nf-fileapi-setfilevaliddata
			static std::once_flag flag;
			std::call_once(flag, acquire_manage_volume_privs);
		}

		handle_type handle = CreateFileW(file_path.c_str(), m.rw_mode
			, FILE_SHARE_READ | FILE_SHARE_WRITE
			, 0, m.create_mode, flags, 0);

		if (handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), system_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		m_file_handle = handle;

		// try to make the file sparse if supported
		// only set this flag if the file is opened for writing
		if ((mode & open_mode::sparse)
			&& (mode & open_mode::rw_mask) != open_mode::read_only)
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

		if ((mode & open_mode::attribute_executable))
			permissions |= S_IXGRP | S_IXOTH | S_IXUSR;
#ifdef O_BINARY
		static const int mode_array[] = {O_RDONLY | O_BINARY, O_WRONLY | O_CREAT | O_BINARY, O_RDWR | O_CREAT | O_BINARY};
#else
		static const int mode_array[] = {O_RDONLY, O_WRONLY | O_CREAT, O_RDWR | O_CREAT};
#endif

		int open_mode = 0
#ifdef O_NOATIME
			| ((mode & open_mode::no_atime) ? O_NOATIME : 0)
#endif
#ifdef O_SYNC
			| ((mode & open_mode::no_cache) ? O_SYNC : 0)
#endif
			;

		handle_type handle = ::open(file_path.c_str()
			, mode_array[static_cast<std::uint32_t>(mode & open_mode::rw_mask)] | open_mode
			, permissions);

#ifdef O_NOATIME
		// O_NOATIME is not allowed for files we don't own
		// so, if we get EPERM when we try to open with it
		// try again without O_NOATIME
		if (handle == -1 && (mode & open_mode::no_atime) && errno == EPERM)
		{
			mode &= ~open_mode::no_atime;
			open_mode &= ~O_NOATIME;
			handle = ::open(file_path.c_str()
				, mode_array[static_cast<std::uint32_t>(mode & open_mode::rw_mask)] | open_mode
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

#ifdef DIRECTIO_ON
		// for solaris
		if ((mode & open_mode::no_cache))
		{
			int yes = 1;
			directio(native_handle(), DIRECTIO_ON);
		}
#endif

#ifdef F_NOCACHE
		// for BSD/Mac
		if ((mode & open_mode::no_cache))
		{
			int yes = 1;
			::fcntl(native_handle(), F_NOCACHE, &yes);

#ifdef F_NODIRECT
			// it's OK to temporarily cache written pages
			::fcntl(native_handle(), F_NODIRECT, &yes);
#endif
		}
#endif

#ifdef POSIX_FADV_RANDOM
		if ((mode & open_mode::random_access))
		{
			// disable read-ahead
			// NOTE: in android this function was introduced in API 21,
			// but the constant POSIX_FADV_RANDOM is there for lower
			// API levels, just don't add :: to allow a macro workaround
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
		open_mode_t const rw_mode = m_open_mode & open_mode::rw_mask;
		if ((rw_mode != open_mode::read_only)
			&& (m_open_mode & open_mode::sparse)
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

		m_open_mode = open_mode_t{};
	}

	namespace {

	void gather_copy(span<iovec_t const> bufs, char* dst)
	{
		std::ptrdiff_t offset = 0;
		for (auto buf : bufs)
		{
			std::copy(buf.begin(), buf.end(), dst + offset);
			offset += buf.size();
		}
	}

	void scatter_copy(span<iovec_t const> bufs, char const* src)
	{
		std::ptrdiff_t offset = 0;
		for (auto buf : bufs)
		{
			std::copy(src + offset, src + offset + buf.size(), buf.data());
			offset += buf.size();
		}
	}

	bool coalesce_read_buffers(span<iovec_t const>& bufs
		, iovec_t& tmp)
	{
		auto const buf_size = bufs_size(bufs);
		auto buf = new char[std::size_t(buf_size)];
		tmp = { buf, buf_size };
		bufs = span<iovec_t const>(tmp);
		return true;
	}

	void coalesce_read_buffers_end(span<iovec_t const> bufs
		, char* const buf, bool const copy)
	{
		if (copy) scatter_copy(bufs, buf);
		delete[] buf;
	}

	bool coalesce_write_buffers(span<iovec_t const>& bufs
		, iovec_t& tmp)
	{
		auto const buf_size = bufs_size(bufs);
		auto buf = new char[std::size_t(buf_size)];
		gather_copy(bufs, buf);
		tmp = { buf, buf_size };
		bufs = span<iovec_t const>(tmp);
		return true;
	}

#if TORRENT_USE_PREADV
namespace {
	int bufs_size(span<::iovec> bufs)
	{
		std::size_t size = 0;
		for (auto buf : bufs) size += buf.iov_len;
		return int(size);
	}
}
#endif // TORRENT_USE_PREADV

	template <class Fun>
	std::int64_t iov(Fun f, handle_type fd, std::int64_t file_offset
		, span<iovec_t const> bufs, error_code& ec)
	{
#if TORRENT_USE_PREADV

		TORRENT_ALLOCA(vec, ::iovec, bufs.size());
		auto it = vec.begin();
		for (auto const& b : bufs)
		{
			it->iov_base = b.data();
			it->iov_len = std::size_t(b.size());
			++it;
		}

		std::int64_t ret = 0;
		while (!vec.empty())
		{
#ifdef IOV_MAX
			auto const nbufs = vec.first(std::min(int(vec.size()), IOV_MAX));
#else
			auto const nbufs = vec;
#endif

			std::int64_t tmp_ret = 0;
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
			int const expected_len = bufs_size(nbufs);
			if (tmp_ret < expected_len) break;

			vec = vec.subspan(nbufs.size());
		}
		return ret;

#elif TORRENT_USE_PREAD

		std::int64_t ret = 0;
		for (auto i : bufs)
		{
			std::int64_t const tmp_ret = f(fd, i.data()
				, static_cast<std::size_t>(i.size()), file_offset);
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
			int tmp_ret = f(fd, i.data(), static_cast<std::size_t>(i.size()));
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

#endif // USE_PREADV
	}

	} // anonymous namespace

	// this has to be thread safe and atomic. i.e. on posix systems it has to be
	// turned into a series of pread() calls
	std::int64_t file::readv(std::int64_t file_offset, span<iovec_t const> bufs
		, error_code& ec, open_mode_t flags)
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
		TORRENT_ASSERT((m_open_mode & open_mode::rw_mask) == open_mode::read_only
			|| (m_open_mode & open_mode::rw_mask) == open_mode::read_write);
		TORRENT_ASSERT(!bufs.empty());
		TORRENT_ASSERT(is_open());

		// there's no point in coalescing single buffer writes
		if (bufs.size() == 1)
		{
			flags &= ~open_mode::coalesce_buffers;
		}

		iovec_t tmp;
		span<iovec_t const> tmp_bufs = bufs;
		if (flags & open_mode::coalesce_buffers)
		{
			if (!coalesce_read_buffers(tmp_bufs, tmp))
				// ok, that failed, don't coalesce this read
				flags &= ~open_mode::coalesce_buffers;
		}

#if TORRENT_USE_PREADV
		std::int64_t ret = iov(&::preadv, native_handle(), file_offset, tmp_bufs, ec);
#elif TORRENT_USE_PREAD
		std::int64_t ret = iov(&::pread, native_handle(), file_offset, tmp_bufs, ec);
#else
		std::int64_t ret = iov(&::read, native_handle(), file_offset, tmp_bufs, ec);
#endif

		if (flags & open_mode::coalesce_buffers)
			coalesce_read_buffers_end(bufs
				, tmp.data(), !ec);

		return ret;
	}

	// This has to be thread safe, i.e. atomic.
	// that means, on posix this has to be turned into a series of
	// pwrite() calls
	std::int64_t file::writev(std::int64_t file_offset, span<iovec_t const> bufs
		, error_code& ec, open_mode_t flags)
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
		TORRENT_ASSERT((m_open_mode & open_mode::rw_mask) == open_mode::write_only
			|| (m_open_mode & open_mode::rw_mask) == open_mode::read_write);
		TORRENT_ASSERT(!bufs.empty());
		TORRENT_ASSERT(is_open());

		ec.clear();

		// there's no point in coalescing single buffer writes
		if (bufs.size() == 1)
		{
			flags &= ~open_mode::coalesce_buffers;
		}

		iovec_t tmp;
		if (flags & open_mode::coalesce_buffers)
		{
			if (!coalesce_write_buffers(bufs, tmp))
				// ok, that failed, don't coalesce writes
				flags &= ~open_mode::coalesce_buffers;
		}

#if TORRENT_USE_PREADV
		std::int64_t ret = iov(&::pwritev, native_handle(), file_offset, bufs, ec);
#elif TORRENT_USE_PREAD
		std::int64_t ret = iov(&::pwrite, native_handle(), file_offset, bufs, ec);
#else
		std::int64_t ret = iov(&::write, native_handle(), file_offset, bufs, ec);
#endif

		if (flags & open_mode::coalesce_buffers)
			delete[] tmp.data();

#if TORRENT_USE_FDATASYNC \
	&& !defined F_NOCACHE && \
	!defined DIRECTIO_ON
		if (m_open_mode & open_mode::no_cache)
		{
			if (::fdatasync(native_handle()) != 0
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

	void set_file_valid_data(HANDLE f, std::int64_t size)
	{
		using SetFileValidData_t = BOOL (WINAPI*)(HANDLE, LONGLONG);
		auto SetFileValidData =
			aux::get_library_procedure<aux::kernel32, SetFileValidData_t>("SetFileValidData");

		if (SetFileValidData)
		{
			// we don't necessarily expect to have enough
			// privilege to do this, so ignore errors.
			SetFileValidData(f, size);
		}
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
			if (!(m_open_mode & open_mode::sparse))
			{
				// if the user has permissions, avoid filling
				// the file with zeroes, but just fill it with
				// garbage instead
				set_file_valid_data(m_file_handle, s);
			}
		}
#else // NON-WINDOWS
		struct stat st{};
		if (::fstat(native_handle(), &st) != 0)
		{
			ec.assign(errno, system_category());
			return false;
		}

		// only truncate the file if it doesn't already
		// have the right size. We don't want to update
		if (st.st_size != s && ::ftruncate(native_handle(), s) < 0)
		{
			ec.assign(errno, system_category());
			return false;
		}

		// if we're not in sparse mode, allocate the storage
		// but only if the number of allocated blocks for the file
		// is less than the file size. Otherwise we would just
		// update the modification time of the file for no good
		// reason.
		if (!(m_open_mode & open_mode::sparse)
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
				// MacOS returns EINVAL if the file already has the space
				// pre-allocated. In which case we can just move on.
				if (errno != EINVAL)
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
			if (ret != 0 && ret != EINVAL && ret != ENOTSUP)
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
		struct stat fs = {};
		if (::fstat(native_handle(), &fs) != 0)
		{
			ec.assign(errno, system_category());
			return -1;
		}
		return fs.st_size;
#endif
	}
}
