/*

Copyright (c) 2003-2014, Arvid Norberg
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

#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES 1

// on mingw this is necessary to enable 64-bit time_t, specifically used for
// the stat struct. Without this, modification times returned by stat may be
// incorrect and consistently fail resume data
#ifndef __MINGW_USE_VC2005_COMPAT
# define __MINGW_USE_VC2005_COMPAT
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/allocator.hpp" // page_size
#include "libtorrent/file.hpp"
#include <cstring>
#include <vector>

#if TORRENT_DEBUG_FILE_LEAKS
#include <set>
#include "libtorrent/thread.hpp"
#endif

// for convert_to_wstring and convert_to_native
#include "libtorrent/aux_/escape_string.hpp"
#include <stdio.h>
#include "libtorrent/assert.hpp"

#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>

#ifdef TORRENT_DISK_STATS
#include "libtorrent/io.hpp"
#endif

#include <sys/stat.h>

#ifdef TORRENT_WINDOWS
// windows part

#ifndef PtrToPtr64
#define PtrToPtr64(x) (x)
#endif

#include "libtorrent/utf8.hpp"

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
#include <errno.h>
#include <dirent.h>

#ifdef TORRENT_LINUX
// linux specifics

#ifdef TORRENT_ANDROID
#include <sys/vfs.h>
#define statvfs statfs
#define fstatvfs fstatfs
#else
#include <sys/statvfs.h>
#endif

#include <sys/ioctl.h>
#ifdef TORRENT_ANDROID
#include <sys/syscall.h>
#define lseek lseek64
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

#elif defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
// mac specifics

#include <copyfile.h>

#endif

#undef _FILE_OFFSET_BITS

// make sure the _FILE_OFFSET_BITS define worked
// on this platform. It's supposed to make file
// related functions support 64-bit offsets.
// this test makes sure lseek() returns a type
// at least 64 bits wide
BOOST_STATIC_ASSERT(sizeof(lseek(0, 0, 0)) >= 8);

#endif // posix part

#if TORRENT_USE_PREADV
# if defined TORRENT_WINDOWS
namespace
{
	// wrap the windows function in something that looks
	// like preadv() and pwritev()

	int preadv(HANDLE fd, libtorrent::file::iovec_t const* bufs, int num_bufs, boost::int64_t file_offset)
	{
		OVERLAPPED* ol = TORRENT_ALLOCA(OVERLAPPED, num_bufs);
		memset(ol, 0, sizeof(OVERLAPPED) * num_bufs);

		HANDLE* h = TORRENT_ALLOCA(HANDLE, num_bufs);

		for (int i = 0; i < num_bufs; ++i)
		{
			ol[i].OffsetHigh = file_offset >> 32;
			ol[i].Offset = file_offset & 0xffffffff;
			ol[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			h[i] = ol[i].hEvent;
			if (h[i] == NULL)
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
			if (ReadFile(fd, bufs[i].iov_base, bufs[i].iov_len, &num_read, &ol[i]) == FALSE
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

		WaitForMultipleObjects(num_bufs, h, TRUE, INFINITE);

		for (int i = 0; i < num_bufs; ++i)
		{
			WaitForSingleObject(ol[i].hEvent, INFINITE);
			DWORD num_read;
			if (GetOverlappedResult(fd, &ol[i], &num_read, FALSE) == FALSE)
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

		for (int i = 0; i < num_bufs; ++i)
			CloseHandle(h[i]);

		return ret;
	}

	int pwritev(HANDLE fd, libtorrent::file::iovec_t const* bufs, int num_bufs, boost::int64_t file_offset)
	{
		OVERLAPPED* ol = TORRENT_ALLOCA(OVERLAPPED, num_bufs);
		memset(ol, 0, sizeof(OVERLAPPED) * num_bufs);

		HANDLE* h = TORRENT_ALLOCA(HANDLE, num_bufs);

		for (int i = 0; i < num_bufs; ++i)
		{
			ol[i].OffsetHigh = file_offset >> 32;
			ol[i].Offset = file_offset & 0xffffffff;
			ol[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			h[i] = ol[i].hEvent;
			if (h[i] == NULL)
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
			if (WriteFile(fd, bufs[i].iov_base, bufs[i].iov_len, &num_written, &ol[i]) == FALSE
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

		WaitForMultipleObjects(num_bufs, h, TRUE, INFINITE);

		for (int i = 0; i < num_bufs; ++i)
		{
			WaitForSingleObject(ol[i].hEvent, INFINITE);
			DWORD num_written;
			if (GetOverlappedResult(fd, &ol[i], &num_written, FALSE) == FALSE)
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

		for (int i = 0; i < num_bufs; ++i)
			CloseHandle(h[i]);

		return ret;
	}
}
# else
#  define _BSD_SOURCE
#  include <sys/uio.h>
# endif
#endif

#ifdef TORRENT_DEBUG
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::sparse) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::attribute_mask) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::sparse & libtorrent::file::attribute_mask) == 0);
#endif

#if defined TORRENT_WINDOWS && defined UNICODE && !TORRENT_USE_WSTRING

#ifdef _MSC_VER
#pragma message ( "wide character support not available. Files will be saved using narrow string names" )
#else
#warning "wide character support not available. Files will be saved using narrow string names"
#endif

#endif // TORRENT_WINDOWS

namespace libtorrent
{
#ifdef TORRENT_WINDOWS
	std::string convert_separators(std::string p)
	{
		for (int i = 0; i < int(p.size()); ++i)
			if (p[i] == '/') p[i] = '\\';
		return p;
	}

	time_t file_time_to_posix(FILETIME f)
	{
		const boost::uint64_t posix_time_offset = 11644473600LL;
		boost::uint64_t ft = (boost::uint64_t(f.dwHighDateTime) << 32)
			| f.dwLowDateTime;

		// windows filetime is specified in 100 nanoseconds resolution.
		// convert to seconds
		return time_t(ft / 10000000 - posix_time_offset);
	}
#endif

	void stat_file(std::string const& inf, file_status* s
		, error_code& ec, int flags)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS

#if TORRENT_USE_WSTRING && defined TORRENT_WINDOWS
#define GetFileAttributesEx_ GetFileAttributesExW
		std::wstring f = convert_to_wstring(inf);

		// apparently windows doesn't expect paths
		// to directories to ever end with a \ or /
		if (!f.empty() && (f[f.size() - 1] == L'\\'
			|| f[f.size() - 1] == L'/'))
			f.resize(f.size() - 1);
#else
#define GetFileAttributesEx_ GetFileAttributesExA
		std::string f = convert_to_native(inf);

		// apparently windows doesn't expect paths
		// to directories to ever end with a \ or /
		if (!f.empty() && (f[f.size() - 1] == '\\'
			|| f[f.size() - 1] == '/'))
			f.resize(f.size() - 1);
#endif
		WIN32_FILE_ATTRIBUTE_DATA data;
		if (!GetFileAttributesEx(f.c_str(), GetFileExInfoStandard, &data))
		{
			ec.assign(GetLastError(), boost::system::system_category());
			return;
		}

		s->file_size = (boost::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
		s->ctime = file_time_to_posix(data.ftCreationTime);
		s->atime = file_time_to_posix(data.ftLastAccessTime);
		s->mtime = file_time_to_posix(data.ftLastWriteTime);

		s->mode = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			? file_status::directory
			: (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE)
			? file_status::character_special : file_status::regular_file;

#else
		
		// posix version

		std::string const& f = convert_to_native(inf);

		struct stat ret;
		int retval;
		if (flags & dont_follow_links)
			retval = ::lstat(f.c_str(), &ret);
		else
			retval = ::stat(f.c_str(), &ret);
		if (retval < 0)
		{
			ec.assign(errno, generic_category());
			return;
		}

		s->file_size = ret.st_size;
		s->atime = ret.st_atime;
		s->mtime = ret.st_mtime;
		s->ctime = ret.st_ctime;

		s->mode = (S_ISREG(ret.st_mode) ? file_status::regular_file : 0)
			| (S_ISDIR(ret.st_mode) ? file_status::directory : 0)
			| (S_ISLNK(ret.st_mode) ? file_status::link : 0)
			| (S_ISFIFO(ret.st_mode) ? file_status::fifo : 0)
			| (S_ISCHR(ret.st_mode) ? file_status::character_special : 0)
			| (S_ISBLK(ret.st_mode) ? file_status::block_special : 0)
			| (S_ISSOCK(ret.st_mode) ? file_status::socket : 0);

#endif // TORRENT_WINDOWS
	}

	void rename(std::string const& inf, std::string const& newf, error_code& ec)
	{
		ec.clear();

#if TORRENT_USE_WSTRING && defined TORRENT_WINDOWS
		std::wstring f1 = convert_to_wstring(inf);
		std::wstring f2 = convert_to_wstring(newf);
		if (_wrename(f1.c_str(), f2.c_str()) < 0)
#else
		std::string const& f1 = convert_to_native(inf);
		std::string const& f2 = convert_to_native(newf);
		if (::rename(f1.c_str(), f2.c_str()) < 0)
#endif
		{
			ec.assign(errno, generic_category());
			return;
		}
	}

	void create_directories(std::string const& f, error_code& ec)
	{
		ec.clear();
		if (is_directory(f, ec)) return;
		if (ec != boost::system::errc::no_such_file_or_directory)
			return;
		ec.clear();
		if (is_root_path(f)) return;
		if (has_parent_path(f))
		{
			create_directories(parent_path(f), ec);
			if (ec) return;
		}
		create_directory(f, ec);
	}

	void create_directory(std::string const& f, error_code& ec)
	{
		ec.clear();

#if defined TORRENT_WINDOWS && TORRENT_USE_WSTRING
#define CreateDirectory_ CreateDirectoryW
		std::wstring n = convert_to_wstring(f);
#else
#define CreateDirectory_ CreateDirectoryA
		std::string const& n = convert_to_native(f);
#endif

#ifdef TORRENT_WINDOWS
		if (CreateDirectory_(n.c_str(), 0) == 0
			&& GetLastError() != ERROR_ALREADY_EXISTS)
			ec.assign(GetLastError(), boost::system::system_category());
#else
		int ret = mkdir(n.c_str(), 0777);
		if (ret < 0 && errno != EEXIST)
			ec.assign(errno, generic_category());
#endif
	}

	bool is_directory(std::string const& f, error_code& ec)
	{
		ec.clear();
		error_code e;
		file_status s;
		stat_file(f, &s, e);
		if (!e && s.mode & file_status::directory) return true;
		ec = e;
		return false;
	}

	void recursive_copy(std::string const& old_path, std::string const& new_path, error_code& ec)
	{
		TORRENT_ASSERT(!ec);
		if (is_directory(old_path, ec))
		{
			create_directory(new_path, ec);
			if (ec) return;
			for (directory i(old_path, ec); !i.done(); i.next(ec))
			{
				std::string f = i.file();
				if (f == ".." || f == ".") continue;
				recursive_copy(combine_path(old_path, f), combine_path(new_path, f), ec);
				if (ec) return;
			}
		}
		else if (!ec)
		{
			copy_file(old_path, new_path, ec);
		}
	}

	void copy_file(std::string const& inf, std::string const& newf, error_code& ec)
	{
		ec.clear();
#if TORRENT_USE_WSTRING && defined TORRENT_WINDOWS
#define CopyFile_ CopyFileW
		std::wstring f1 = convert_to_wstring(inf);
		std::wstring f2 = convert_to_wstring(newf);
#else
#define CopyFile_ CopyFileA
		std::string const& f1 = convert_to_native(inf);
		std::string const& f2 = convert_to_native(newf);
#endif

#ifdef TORRENT_WINDOWS
		if (CopyFile_(f1.c_str(), f2.c_str(), false) == 0)
			ec.assign(GetLastError(), boost::system::system_category());
#elif defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
		// this only works on 10.5
		copyfile_state_t state = copyfile_state_alloc();
		if (copyfile(f1.c_str(), f2.c_str(), state, COPYFILE_ALL) < 0)
			ec.assign(errno, generic_category());
		copyfile_state_free(state);
#else
		int infd = ::open(inf.c_str(), O_RDONLY);
		if (infd < 0)
		{
			ec.assign(errno, generic_category());
			return;
		}

		// rely on default umask to filter x and w permissions
		// for group and others
		int permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		int outfd = ::open(newf.c_str(), O_WRONLY | O_CREAT, permissions);
		if (outfd < 0)
		{
			close(infd);
			ec.assign(errno, generic_category());
			return;
		}
		char buffer[4096];
		for (;;)
		{
			int num_read = read(infd, buffer, sizeof(buffer));
			if (num_read == 0) break;
			if (num_read < 0)
			{
				ec.assign(errno, generic_category());
				break;
			}
			int num_written = write(outfd, buffer, num_read);
			if (num_written < num_read)
			{
				ec.assign(errno, generic_category());
				break;
			}
			if (num_read < int(sizeof(buffer))) break;
		}
		close(infd);
		close(outfd);
#endif // TORRENT_WINDOWS
	}

	std::string split_path(std::string const& f)
	{
		if (f.empty()) return f;

		std::string ret;
		char const* start = f.c_str();
		char const* p = start;
		while (*start != 0)
		{
			while (*p != '/'
				&& *p != '\0'
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
				&& *p != '\\'
#endif
				) ++p;
			if (p - start > 0)
			{
				ret.append(start, p - start);
				ret.append(1, '\0');
			}
			if (*p != 0) ++p;
			start = p;
		}
		ret.append(1, '\0');
		return ret;
	}

	char const* next_path_element(char const* p)
	{
		p += strlen(p) + 1;
		if (*p == 0) return 0;
		return p;
	}

	std::string extension(std::string const& f)
	{
		for (int i = f.size() - 1; i >= 0; --i)
		{
			if (f[i] == '/') break;
#ifdef TORRENT_WINDOWS
			if (f[i] == '\\') break;
#endif
			if (f[i] != '.') continue;
			return f.substr(i);
		}
		return "";
	}

	std::string remove_extension(std::string const& f)
	{
		char const* slash = strrchr(f.c_str(), '/');
#ifdef TORRENT_WINDOWS
		slash = (std::max)((char const*)strrchr(f.c_str(), '\\'), slash);
#endif
		char const* ext = strrchr(f.c_str(), '.');
		// if we don't have an extension, just return f
		if (ext == 0 || ext == &f[0] || (slash != NULL && ext < slash)) return f;
		return f.substr(0, ext - &f[0]);
	}

	void replace_extension(std::string& f, std::string const& ext)
	{
		for (int i = f.size() - 1; i >= 0; --i)
		{
			if (f[i] == '/') break;
#ifdef TORRENT_WINDOWS
			if (f[i] == '\\') break;
#endif

			if (f[i] != '.') continue;

			f.resize(i);
			break;
		}
		f += '.';
		f += ext;
	}

	bool is_root_path(std::string const& f)
	{
		if (f.empty()) return false;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		// match \\ form
		if (f == "\\\\") return true;
		int i = 0;
		// match the xx:\ or xx:/ form
		while (f[i] && is_alpha(f[i])) ++i;
		if (i == int(f.size()-2) && f[i] == ':' && (f[i+1] == '\\' || f[i+1] == '/'))
			return true;
		// match network paths \\computer_name\ form
		if (f.size() > 2 && f[0] == '\\' && f[1] == '\\')
		{
			// we don't care about the last character, since it's OK for it
			// to be a slash or a back slash
			bool found = false;
			for (int i = 2; i < int(f.size()) - 1; ++i)
			{
				if (f[i] != '\\' && f[i] != '/') continue;
				// there is a directory separator in here,
				// i.e. this is not the root
				found = true;
				break;
			}
			if (!found) return true;
		}
#else
		// as well as parent_path("/") should be "/".
		if (f == "/") return true;
#endif
		return false;
	}

	bool has_parent_path(std::string const& f)
	{
		if (f.empty()) return false;
		if (is_root_path(f)) return false;

		int len = f.size() - 1;
		// if the last character is / or \ ignore it
		if (f[len] == '/' || f[len] == '\\') --len;
		while (len >= 0)
		{
			if (f[len] == '/' || f[len] == '\\')
				break;
			--len;
		}

		return len >= 0;
	}

	std::string parent_path(std::string const& f)
	{
		if (f.empty()) return f;

#ifdef TORRENT_WINDOWS
		if (f == "\\\\") return "";
#endif
		if (f == "/") return "";

		int len = f.size();
		// if the last character is / or \ ignore it
		if (f[len-1] == '/' || f[len-1] == '\\') --len;
		while (len > 0)
		{
			--len;
			if (f[len] == '/' || f[len] == '\\')
				break;
		}

		if (f[len] == '/' || f[len] == '\\') ++len;
		return std::string(f.c_str(), len);
	}

	char const* filename_cstr(char const* f)
	{
		if (f == 0) return f;

		char const* sep = strrchr(f, '/');
#ifdef TORRENT_WINDOWS
		char const* altsep = strrchr(f, '\\');
		if (sep == 0 || altsep > sep) sep = altsep;
#endif
		if (sep == 0) return f;
		return sep+1;
	}

	std::string filename(std::string const& f)
	{
		if (f.empty()) return "";
		char const* first = f.c_str();
		char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		char const* altsep = strrchr(first, '\\');
		if (sep == 0 || altsep > sep) sep = altsep;
#endif
		if (sep == 0) return f;

		if (sep - first == int(f.size()) - 1)
		{
			// if the last character is a / (or \)
			// ignore it
			int len = 0;
			while (sep > first)
			{
				--sep;
				if (*sep == '/'
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
					|| *sep == '\\'
#endif
					)
					return std::string(sep + 1, len);
				++len;
			}
			return std::string(first, len);
			
		}
		return std::string(sep + 1);
	}

	void append_path(std::string& branch, std::string const& leaf)
	{
		append_path(branch, leaf.c_str(), leaf.size());
	}

	void append_path(std::string& branch
		, char const* str, int len)
	{
		TORRENT_ASSERT(!is_complete(std::string(str, len)));
		if (branch.empty() || branch == ".")
		{
			branch.assign(str, len);
			return;
		}
		if (len == 0) return;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR_CHAR '\\'
		bool need_sep = branch[branch.size()-1] != '\\'
			&& branch[branch.size()-1] != '/';
#else
#define TORRENT_SEPARATOR_CHAR '/'
		bool need_sep = branch[branch.size()-1] != '/';
#endif

		if (need_sep) branch += TORRENT_SEPARATOR_CHAR;
		branch.append(str, len);
	}

	std::string combine_path(std::string const& lhs, std::string const& rhs)
	{
		TORRENT_ASSERT(!is_complete(rhs));
		if (lhs.empty() || lhs == ".") return rhs;
		if (rhs.empty() || rhs == ".") return lhs;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR "\\"
		bool need_sep = lhs[lhs.size()-1] != '\\' && lhs[lhs.size()-1] != '/';
#else
#define TORRENT_SEPARATOR "/"
		bool need_sep = lhs[lhs.size()-1] != '/';
#endif
		std::string ret;
		int target_size = lhs.size() + rhs.size() + 2;
		ret.resize(target_size);
		target_size = snprintf(&ret[0], target_size, "%s%s%s", lhs.c_str()
			, (need_sep?TORRENT_SEPARATOR:""), rhs.c_str());
		ret.resize(target_size);
		return ret;
	}

	std::string current_working_directory()
	{
#if defined TORRENT_WINDOWS && !defined TORRENT_MINGW
#if TORRENT_USE_WSTRING
		wchar_t cwd[TORRENT_MAX_PATH];
		_wgetcwd(cwd, sizeof(cwd) / sizeof(wchar_t));
#else
		char cwd[TORRENT_MAX_PATH];
		_getcwd(cwd, sizeof(cwd));
#endif // TORRENT_USE_WSTRING
#else
		char cwd[TORRENT_MAX_PATH];
		if (getcwd(cwd, sizeof(cwd)) == 0) return "/";
#endif
#if defined TORRENT_WINDOWS && !defined TORRENT_MINGW && TORRENT_USE_WSTRING
		return convert_from_wstring(cwd);
#else
		return convert_from_native(cwd);
#endif
	}

#if TORRENT_USE_UNC_PATHS
	std::string canonicalize_path(std::string const& f)
	{
		std::string ret;
		ret.resize(f.size());
		char* write_cur = &ret[0];
		char* last_write_sep = write_cur;

		char const* read_cur = f.c_str();
		char const* last_read_sep = read_cur;

		// the last_*_sep pointers point to one past
		// the last path separator encountered and is
		// initializes to the first character in the path
		while (*read_cur)
		{
			if (*read_cur != '\\')
			{
				*write_cur++ = *read_cur++;
				continue;
			}
			int element_len = read_cur - last_read_sep;
			if (element_len == 1 && memcmp(last_read_sep, ".", 1) == 0)
			{
				--write_cur;
				++read_cur;
				last_read_sep = read_cur;
				continue;
			}
			if (element_len == 2 && memcmp(last_read_sep, "..", 2) == 0)
			{
				// find the previous path separator
				if (last_write_sep > &ret[0])
				{
					--last_write_sep;
					while (last_write_sep > &ret[0]
						&& last_write_sep[-1] != '\\')
						--last_write_sep;
				}
				write_cur = last_write_sep;
				// find the previous path separator
				if (last_write_sep > &ret[0])
				{
					--last_write_sep;
					while (last_write_sep > &ret[0]
						&& last_write_sep[-1] != '\\')
						--last_write_sep;
				}
				++read_cur;
				last_read_sep = read_cur;
				continue;
			}
			*write_cur++ = *read_cur++;
			last_write_sep = write_cur;
			last_read_sep = read_cur;
		}
		// terminate destination string
		*write_cur = 0;
		ret.resize(write_cur - &ret[0]);
		return ret;
	}
#endif	

	boost::int64_t file_size(std::string const& f)
	{
		error_code ec;
		file_status s;
		stat_file(f, &s, ec);
		if (ec) return 0;
		return s.file_size;
	}

	bool exists(std::string const& f, error_code& ec)
	{
		file_status s;
		stat_file(f, &s, ec);
		if (ec)
		{
			if (ec == boost::system::errc::no_such_file_or_directory)
				ec.clear();
			return false;
		}
		return true;
	}

	bool exists(std::string const& f)
	{
		error_code ec;
		return exists(f, ec);
	}

	void remove(std::string const& inf, error_code& ec)
	{
		ec.clear();

#ifdef TORRENT_WINDOWS
		// windows does not allow trailing / or \ in
		// the path when removing files
		std::string pruned;
		if (inf[inf.size() - 1] == '/'
			|| inf[inf.size() - 1] == '\\')
			pruned = inf.substr(0, inf.size() - 1);
		else
			pruned = inf;
#if TORRENT_USE_WSTRING
#define DeleteFile_ DeleteFileW
#define RemoveDirectory_ RemoveDirectoryW
		std::wstring f = convert_to_wstring(pruned);
#else
#define DeleteFile_ DeleteFileA
#define RemoveDirectory_ RemoveDirectoryA
		std::string f = convert_to_native(pruned);
#endif
		if (DeleteFile_(f.c_str()) == 0)
		{
			if (GetLastError() == ERROR_ACCESS_DENIED)
			{
				if (RemoveDirectory_(f.c_str()) != 0)
					return;
			}
			ec.assign(GetLastError(), boost::system::system_category());
			return;
		}
#else // TORRENT_WINDOWS
		std::string const& f = convert_to_native(inf);
		if (::remove(f.c_str()) < 0)
		{
			ec.assign(errno, generic_category());
			return;
		}
#endif // TORRENT_WINDOWS
	}

	void remove_all(std::string const& f, error_code& ec)
	{
		ec.clear();

		file_status s;
		stat_file(f, &s, ec);
		if (ec) return;

		if (s.mode & file_status::directory)
		{
			for (directory i(f, ec); !i.done(); i.next(ec))
			{
				if (ec) return;
				std::string p = i.file();
				if (p == "." || p == "..") continue;
				remove_all(combine_path(f, p), ec);
				if (ec) return;
			}
		}
		remove(f, ec);
	}

	std::string complete(std::string const& f)
	{
		if (is_complete(f)) return f;
		if (f == ".") return current_working_directory();
		return combine_path(current_working_directory(), f);
	}

	bool is_complete(std::string const& f)
	{
		if (f.empty()) return false;
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		int i = 0;
		// match the xx:\ or xx:/ form
		while (f[i] && is_alpha(f[i])) ++i;
		if (i < int(f.size()-1) && f[i] == ':' && (f[i+1] == '\\' || f[i+1] == '/'))
			return true;

		// match the \\ form
		if (int(f.size()) >= 2 && f[0] == '\\' && f[1] == '\\')
			return true;
		return false;
#else
		if (f[0] == '/') return true;
		return false;
#endif
	}

	directory::directory(std::string const& path, error_code& ec)
		: m_done(false)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
		m_inode = 0;
		// the path passed to FindFirstFile() must be
		// a pattern
		std::string f = convert_separators(path);
		if (!f.empty() && f[f.size()-1] != '\\') f += "\\*";
		else f += "*";
#if TORRENT_USE_WSTRING
#define FindFirstFile_ FindFirstFileW
		std::wstring p = convert_to_wstring(f);
#else
#define FindFirstFile_ FindFirstFileA
		std::string p = convert_to_native(f);
#endif
		m_handle = FindFirstFile_(p.c_str(), &m_fd);
		if (m_handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), boost::system::system_category());
			m_done = true;
			return;
		}
#else

		memset(&m_dirent, 0, sizeof(dirent));
		m_name[0] = 0;

		// the path passed to opendir() may not
		// end with a /
		std::string p = path;
		if (!path.empty() && path[path.size()-1] == '/')
			p.resize(path.size()-1);

		p = convert_to_native(p);
		m_handle = opendir(p.c_str());
		if (m_handle == 0)
		{
			ec.assign(errno, generic_category());
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

	boost::uint64_t directory::inode() const
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
#if TORRENT_USE_WSTRING
		return convert_from_wstring(m_fd.cFileName);
#else
		return convert_from_native(m_fd.cFileName);
#endif
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
				ec.assign(err, boost::system::system_category());
		}
		++m_inode;
#else
		dirent* dummy;
		if (readdir_r(m_handle, &m_dirent, &dummy) != 0)
		{
			ec.assign(errno, generic_category());
			m_done = true;
		}
		if (dummy == 0) m_done = true;
#endif
	}

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE -1
#endif

#ifdef TORRENT_WINDOWS
	struct overlapped_t
	{
		overlapped_t()
		{
			memset(&ol, 0, sizeof(ol));
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

			DWORD ret = -1;
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
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		, m_sector_size(0)
#endif
	{
#ifdef TORRENT_DISK_STATS
		m_file_id = 0;
#endif
	}

	file::file(std::string const& path, int mode, error_code& ec)
		: m_file_handle(INVALID_HANDLE_VALUE)
		, m_open_mode(0)
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		, m_sector_size(0)
#endif
	{
#ifdef TORRENT_DISK_STATS
		m_file_id = 0;
#endif
		// the return value is not important, since the
		// error code contains the same information
		open(path, mode, ec);
	}

	file::~file()
	{
		close();
	}

#ifdef TORRENT_DISK_STATS
	boost::uint32_t silly_hash(std::string const& str)
	{
		boost::uint32_t ret = 1;
		for (int i = 0; i < str.size(); ++i)
		{
			if (str[i] == 0) continue;
			ret *= int(str[i]);
		}
		return ret;
	}
#endif

	bool file::open(std::string const& path, int mode, error_code& ec)
	{
		close();

#if TORRENT_DEBUG_FILE_LEAKS
		m_file_path = path;
#endif

#ifdef TORRENT_DISK_STATS
		m_file_id = silly_hash(path);
#endif

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

		std::string p = convert_separators(path);
#if TORRENT_USE_UNC_PATHS
		// UNC paths must be absolute
		// network paths are already UNC paths
		if (path.substr(0,2) == "\\\\") p = path;
		else p = "\\\\?\\" + (is_complete(p) ? p : combine_path(current_working_directory(), p));
#endif

#if TORRENT_USE_WSTRING
#define CreateFile_ CreateFileW
		m_path = convert_to_wstring(p);
#else
#define CreateFile_ CreateFileA
		m_path = convert_to_native(p);
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
			| ((mode & direct_io) ? FILE_FLAG_NO_BUFFERING : 0)
			| ((mode & no_cache) ? FILE_FLAG_WRITE_THROUGH : 0);

		handle_type handle = CreateFile_(m_path.c_str(), m.rw_mode
			, (mode & lock_file) ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE
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
#ifdef O_DIRECT
			| ((mode & direct_io) ? O_DIRECT : 0)
#endif
#ifdef O_SYNC
			| ((mode & no_cache) ? O_SYNC: 0)
#endif
			;

 		handle_type handle = ::open(convert_to_native(path).c_str()
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
			handle = ::open(path.c_str(), mode_array[mode & rw_mask] | open_mode
				, permissions);
		}
#endif
		if (handle == -1)
		{
			ec.assign(errno, get_posix_category());
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

#if TORRENT_DEBUG_FILE_LEAKS
	void file::print_info(FILE* out) const
	{
		if (!is_open()) return;
		fprintf(out, "\n===> FILE: %s\n", m_file_path.c_str());
	}
#endif

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
		if (ol.ol.hEvent == NULL) return false;

#ifdef TORRENT_MINGW
typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
	LARGE_INTEGER FileOffset;
	LARGE_INTEGER Length;
} FILE_ALLOCATED_RANGE_BUFFER, *PFILE_ALLOCATED_RANGE_BUFFER;
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
			int error = GetLastError();
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
#ifdef TORRENT_DISK_STATS
		m_file_id = 0;
#endif
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		m_sector_size = 0;
#endif

		if (!is_open()) return;

#ifdef TORRENT_WINDOWS

		// if this file is open for writing, has the sparse
		// flag set, but there are no sparse regions, unset
		// the flag
		int rw_mode = m_open_mode & rw_mask;
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
			} FILE_SET_SPARSE_BUFFER, *PFILE_SET_SPARSE_BUFFER;
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
		m_path.clear();
#else
		if (m_file_handle != INVALID_HANDLE_VALUE)
			::close(m_file_handle);
#endif

		m_file_handle = INVALID_HANDLE_VALUE;

		m_open_mode = 0;
	}

	// defined in storage.cpp
	int bufs_size(file::iovec_t const* bufs, int num_bufs);
	
	void gather_copy(file::iovec_t const* bufs, int num_bufs, char* dst)
	{
		int offset = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			memcpy(dst + offset, bufs[i].iov_base, bufs[i].iov_len);
			offset += bufs[i].iov_len;
		}
	}

	void scatter_copy(file::iovec_t const* bufs, int num_bufs, char const* src)
	{
		int offset = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			memcpy(bufs[i].iov_base, src + offset, bufs[i].iov_len);
			offset += bufs[i].iov_len;
		}
	}

	bool coalesce_read_buffers(file::iovec_t const*& bufs, int& num_bufs, file::iovec_t* tmp)
	{
		int buf_size = bufs_size(bufs, num_bufs);
		// this is page aligned since it's used in APIs which
		// are likely to require that (depending on OS)
		char* buf = (char*)page_aligned_allocator::malloc(buf_size);
		if (!buf) return false;
		tmp->iov_base = buf;
		tmp->iov_len = buf_size;
		bufs = tmp;
		num_bufs = 1;
		return true;
	}

	void coalesce_read_buffers_end(file::iovec_t const* bufs, int num_bufs, char* buf, bool copy)
	{
		if (copy) scatter_copy(bufs, num_bufs, buf);
		page_aligned_allocator::free(buf);
	}

	bool coalesce_write_buffers(file::iovec_t const*& bufs, int& num_bufs, file::iovec_t* tmp)
	{
		// coalesce buffers means allocate a temporary buffer and
		// issue a single write operation instead of using a vector
		// operation
		int buf_size = 0;
		for (int i = 0; i < num_bufs; ++i) buf_size += bufs[i].iov_len;
		char* buf = (char*)page_aligned_allocator::malloc(buf_size);
		if (!buf) return false;
		gather_copy(bufs, num_bufs, buf);
		tmp->iov_base = buf;
		tmp->iov_len = buf_size;
		bufs = tmp;
		num_bufs = 1;
		return true;
	}

	template <class Fun>
	boost::int64_t iov(Fun f, handle_type fd, boost::int64_t file_offset, file::iovec_t const* bufs_in
		, int num_bufs_in, error_code& ec)
	{
		file::iovec_t const* bufs = bufs_in;
		int num_bufs = num_bufs_in;

#if TORRENT_USE_PREADV

		int ret = 0;
		while (num_bufs > 0)
		{
			int nbufs = (std::min)(num_bufs, TORRENT_IOV_MAX);
			int tmp_ret = 0;
			tmp_ret = f(fd, bufs, nbufs, file_offset);
			if (tmp_ret < 0)
			{
#ifdef TORRENT_WINDOWS
				ec.assign(GetLastError(), system_category());
#else
				ec.assign(errno, get_posix_category());
#endif
				return -1;
			}
			file_offset += tmp_ret;
			ret += tmp_ret;

			num_bufs -= nbufs;
			bufs += nbufs;
		}
		return ret;

#elif TORRENT_USE_PREAD

		int ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int tmp_ret = f(fd, i->iov_base, i->iov_len, file_offset);
			if (tmp_ret < 0)
			{
#ifdef TORRENT_WINDOWS
				ec.assign(GetLastError(), system_category());
#else
				ec.assign(errno, get_posix_category());
#endif
				return -1;
			}
			file_offset += tmp_ret;
			ret += tmp_ret;
			if (tmp_ret < int(i->iov_len)) break;
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
			ec.assign(errno, get_posix_category());
			return -1;
		}
#endif

		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int tmp_ret = f(fd, i->iov_base, i->iov_len);
			if (tmp_ret < 0)
			{
#ifdef TORRENT_WINDOWS
				ec.assign(GetLastError(), system_category());
#else
				ec.assign(errno, get_posix_category());
#endif
				return -1;
			}
			file_offset += tmp_ret;
			ret += tmp_ret;
			if (tmp_ret < int(i->iov_len)) break;
		}

		return ret;

#endif
	}

	// this has to be thread safe and atomic. i.e. on posix systems it has to be
	// turned into a series of pread() calls
	boost::int64_t file::readv(boost::int64_t file_offset, iovec_t const* bufs, int num_bufs
		, error_code& ec, int flags)
	{
		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
#ifdef TORRENT_WINDOWS
			ec = error_code(ERROR_INVALID_HANDLE, system_category());
#else
			ec = error_code(EBADF, system_category());
#endif
			return -1;
		}
		TORRENT_ASSERT((m_open_mode & rw_mask) == read_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(is_open());

#if TORRENT_USE_PREADV
		int ret = iov(&::preadv, native_handle(), file_offset, bufs, num_bufs, ec);
#else

		file::iovec_t tmp;
		if (flags & file::coalesce_buffers)
		{
			if (!coalesce_read_buffers(bufs, num_bufs, &tmp))
				// ok, that failed, don't coalesce this read
				flags &= ~file::coalesce_buffers;
		}

#if TORRENT_USE_PREAD
		int ret = iov(&::pread, native_handle(), file_offset, bufs, num_bufs, ec);
#else
		int ret = iov(&::read, native_handle(), file_offset, bufs, num_bufs, ec);
#endif

		if (flags & file::coalesce_buffers)
			coalesce_read_buffers_end(bufs, num_bufs, (char*)tmp.iov_base, !ec);

#endif
		return ret;
	}

	// This has to be thread safe, i.e. atomic.
	// that means, on posix this has to be turned into a series of
	// pwrite() calls
	boost::int64_t file::writev(boost::int64_t file_offset, iovec_t const* bufs, int num_bufs
		, error_code& ec, int flags)
	{
		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
#ifdef TORRENT_WINDOWS
			ec = error_code(ERROR_INVALID_HANDLE, system_category());
#else
			ec = error_code(EBADF, system_category());
#endif
			return -1;
		}
		TORRENT_ASSERT((m_open_mode & rw_mask) == write_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(is_open());

		ec.clear();

#if TORRENT_USE_PREADV
		int ret = iov(&::pwritev, native_handle(), file_offset, bufs, num_bufs, ec);
#else

		file::iovec_t tmp;
		if (flags & file::coalesce_buffers)
		{
			if (!coalesce_write_buffers(bufs, num_bufs, &tmp))
				// ok, that failed, don't coalesce writes
				flags &= ~file::coalesce_buffers;
		}

#if TORRENT_USE_PREAD
		int ret = iov(&::pwrite, native_handle(), file_offset, bufs, num_bufs, ec);
#else
		int ret = iov(&::write, native_handle(), file_offset, bufs, num_bufs, ec);
#endif

		if (flags & file::coalesce_buffers)
			free(tmp.iov_base);

#endif
#if TORRENT_HAVE_FDATASYNC \
	&& !defined F_NOCACHE && \
	!defined DIRECTIO_ON
		if (m_open_mode & no_cache)
		{
			if (fdatasync(native_handle()) != 0
				&& errno != EINVAL
				&& errno != ENOSYS)
			{
				ec.assign(errno, get_posix_category());
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

		static OpenProcessToken_t pOpenProcessToken = NULL;
		static LookupPrivilegeValue_t pLookupPrivilegeValue = NULL;
		static AdjustTokenPrivileges_t pAdjustTokenPrivileges = NULL;
		static bool failed_advapi = false;

		if (pOpenProcessToken == NULL && !failed_advapi)
		{
			HMODULE advapi = LoadLibraryA("advapi32");
			if (advapi == NULL)
			{
				failed_advapi = true;
				return false;
			}
			pOpenProcessToken = (OpenProcessToken_t)GetProcAddress(advapi, "OpenProcessToken");
			pLookupPrivilegeValue = (LookupPrivilegeValue_t)GetProcAddress(advapi, "LookupPrivilegeValueA");
			pAdjustTokenPrivileges = (AdjustTokenPrivileges_t)GetProcAddress(advapi, "AdjustTokenPrivileges");
			if (pOpenProcessToken == NULL
				|| pLookupPrivilegeValue == NULL
				|| pAdjustTokenPrivileges == NULL)
			{
				failed_advapi = true;
				return false;
			}
		}

		HANDLE token;
		if (!pOpenProcessToken(GetCurrentProcess()
			, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
			return false;

		TOKEN_PRIVILEGES privs;
		if (!pLookupPrivilegeValue(NULL, "SeManageVolumePrivilege"
			, &privs.Privileges[0].Luid))
		{
			CloseHandle(token);
			return false;
		}

		privs.PrivilegeCount = 1;
		privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		bool ret = pAdjustTokenPrivileges(token, FALSE, &privs, 0, NULL, NULL)
			&& GetLastError() == ERROR_SUCCESS;

		CloseHandle(token);

		return ret;
	}

	void set_file_valid_data(HANDLE f, boost::int64_t size)
	{
		typedef BOOL (WINAPI *SetFileValidData_t)(HANDLE, LONGLONG);
		static SetFileValidData_t pSetFileValidData = NULL;
		static bool failed_kernel32 = false;

		if (pSetFileValidData == NULL && !failed_kernel32)
		{
			HMODULE k32 = LoadLibraryA("kernel32");
			if (k32 == NULL)
			{
				failed_kernel32 = true;
				return;
			}
			pSetFileValidData = (SetFileValidData_t)GetProcAddress(k32, "SetFileValidData");
			if (pSetFileValidData == NULL)
			{
				failed_kernel32 = true;
				return;
			}
		}

		TORRENT_ASSERT(pSetFileValidData);

		// we don't necessarily expect to have enough
		// privilege to do this, so ignore errors.
		pSetFileValidData(f, size);
	}
#endif

  	bool file::set_size(boost::int64_t s, error_code& ec)
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

		if ((m_open_mode & sparse) == 0)
		{
#if TORRENT_USE_WSTRING
			typedef DWORD (WINAPI *GetCompressedFileSize_t)(LPCWSTR lpFileName, LPDWORD lpFileSizeHigh);
#else
			typedef DWORD (WINAPI *GetCompressedFileSize_t)(LPCSTR lpFileName, LPDWORD lpFileSizeHigh);
#endif

			static GetCompressedFileSize_t GetCompressedFileSize_ = NULL;

			static bool failed_kernel32 = false;

			if ((GetCompressedFileSize_ == NULL) && !failed_kernel32)
			{
				HMODULE kernel32 = LoadLibraryA("kernel32.dll");
				if (kernel32)
				{
#if TORRENT_USE_WSTRING
					GetCompressedFileSize_ = (GetCompressedFileSize_t)GetProcAddress(kernel32, "GetCompressedFileSizeW");
#else
					GetCompressedFileSize_ = (GetCompressedFileSize_t)GetProcAddress(kernel32, "GetCompressedFileSizeA");
#endif
				}
				else
				{
					failed_kernel32 = true;
				}
			}

			offs.QuadPart = 0;
			if (GetCompressedFileSize_)
			{
				// only allocate the space if the file
				// is not fully allocated
				DWORD high_dword = 0;
				offs.LowPart = GetCompressedFileSize_(m_path.c_str(), &high_dword);
				offs.HighPart = high_dword;
				if (offs.LowPart == INVALID_FILE_SIZE)
				{
					ec.assign(GetLastError(), system_category());
					if (ec) return false;
				}
			}

			if (offs.QuadPart != s)
			{
				// if the user has permissions, avoid filling
				// the file with zeroes, but just fill it with
				// garbage instead
				set_file_valid_data(m_file_handle, s);
			}
		}
#else // NON-WINDOWS
		struct stat st;
		if (fstat(native_handle(), &st) != 0)
		{
			ec.assign(errno, get_posix_category());
			return false;
		}

		// only truncate the file if it doesn't already
		// have the right size. We don't want to update
		if (st.st_size != s && ftruncate(native_handle(), s) < 0)
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
			if (fcntl(native_handle(), F_PREALLOCATE, &f) < 0)
			{
				if (errno != ENOSPC)
				{
					ec.assign(errno, get_posix_category());
					return false;
				}
				// ok, let's try to allocate non contiguous space then
				fstore_t f = {F_ALLOCATEALL, F_PEOFPOSMODE, 0, s, 0};
				if (fcntl(native_handle(), F_PREALLOCATE, &f) < 0)
				{
					ec.assign(errno, get_posix_category());
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
				ec.assign(errno, get_posix_category());
				return false;
			}

#endif // F_ALLOCSP64

#if defined TORRENT_LINUX || TORRENT_HAS_FALLOCATE
			int ret;
#endif

#if defined TORRENT_LINUX
			ret = my_fallocate(native_handle(), 0, 0, s);
			// if we return 0, everything went fine
			// the fallocate call succeeded
			if (ret == 0) return true;
			// otherwise, something went wrong. If the error
			// is ENOSYS, just keep going and do it the old-fashioned
			// way. If fallocate failed with some other error, it
			// probably means the user should know about it, error out
			// and report it.
			if (errno != ENOSYS && errno != EOPNOTSUPP && errno != EINVAL)
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
			ret = posix_fallocate(native_handle(), 0, s);
			// posix_allocate fails with EINVAL in case the underlying
			// filesystem does bot support this operation
			if (ret != 0 && ret != EINVAL)
			{
				ec.assign(ret, get_posix_category());
				return false;
			}
#endif // TORRENT_HAS_FALLOCATE
		}
#endif // TORRENT_WINDOWS
		return true;
	}

	boost::int64_t file::get_size(error_code& ec) const
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
			ec.assign(errno, get_posix_category());
			return -1;
		}
		return fs.st_size;
#endif
	}

	boost::int64_t file::sparse_end(boost::int64_t start) const
	{
#ifdef TORRENT_WINDOWS

#ifdef TORRENT_MINGW
typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
	LARGE_INTEGER FileOffset;
	LARGE_INTEGER Length;
} FILE_ALLOCATED_RANGE_BUFFER, *PFILE_ALLOCATED_RANGE_BUFFER;
#define FSCTL_QUERY_ALLOCATED_RANGES ((0x9 << 16) | (1 << 14) | (51 << 2) | 3)
#endif // TORRENT_MINGW

		FILE_ALLOCATED_RANGE_BUFFER buffer;
		DWORD bytes_returned = 0;
		FILE_ALLOCATED_RANGE_BUFFER in;
		error_code ec;
		boost::int64_t file_size = get_size(ec);
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
		boost::int64_t ret = lseek(native_handle(), start, SEEK_DATA);
		if (ret < 0) return start;
		return start;
#else
		return start;
#endif
	}

#if TORRENT_DEBUG_FILE_LEAKS
	std::set<file_handle*> global_file_handles;
	mutex file_handle_mutex;

	file_handle::file_handle()
	{
		mutex::scoped_lock l(file_handle_mutex);
		global_file_handles.insert(this);
		stack[0] = 0;
	}
	file_handle::file_handle(file* f): m_file(f)
	{
		mutex::scoped_lock l(file_handle_mutex);
		global_file_handles.insert(this);
		if (f) print_backtrace(stack, sizeof(stack), 10);
		else stack[0] = 0;
	}
	file_handle::file_handle(file_handle const& fh)
	{
		mutex::scoped_lock l(file_handle_mutex);
		global_file_handles.insert(this);
		m_file = fh.m_file;
		if (m_file) print_backtrace(stack, sizeof(stack), 10);
		else stack[0] = 0;
	}
	file_handle::~file_handle()
	{
		mutex::scoped_lock l(file_handle_mutex);
		global_file_handles.erase(this);
		stack[0] = 0;
	}
	file* file_handle::operator->() { return m_file.get(); }
	file const* file_handle::operator->() const { return m_file.get(); }
	file& file_handle::operator*() { return *m_file.get(); }
	file const& file_handle::operator*() const { return *m_file.get(); }
	file* file_handle::get() { return m_file.get(); }
	file const* file_handle::get() const { return m_file.get(); }
	file_handle::operator bool() const { return m_file.get(); }
	file_handle& file_handle::reset(file* f)
	{
		mutex::scoped_lock l(file_handle_mutex);
		if (f) print_backtrace(stack, sizeof(stack), 10);
		else stack[0] = 0;
		l.unlock();
		m_file.reset(f);
		return *this;
	}

	void print_open_files(char const* event, char const* name)
	{
		FILE* out = fopen("open_files.log", "a+");
		mutex::scoped_lock l(file_handle_mutex);
		fprintf(out, "\n\nEVENT: %s TORRENT: %s\n\n", event, name);
		for (std::set<file_handle*>::iterator i = global_file_handles.begin()
			, end(global_file_handles.end()); i != end; ++i)
		{
			TORRENT_ASSERT(*i != NULL);
			if (!*i) continue;
			file_handle const& h = **i;
			if (!h) continue;

			if (!h->is_open()) continue;
			h->print_info(out);
			fprintf(out, "\n%s\n\n", h.stack);
		}
		fclose(out);
	}
#endif
}

