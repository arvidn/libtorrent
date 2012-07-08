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
#include "libtorrent/escape_string.hpp" // for string conversion
#include "libtorrent/file.hpp"
#include <cstring>
#include <vector>

// for convert_to_wstring and convert_to_native
#include "libtorrent/escape_string.hpp"
#include <stdio.h>
#include "libtorrent/assert.hpp"

#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>

#ifdef TORRENT_DISK_STATS
#include "libtorrent/io.hpp"
#endif

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
#include <direct.h> // for _getcwd, _mkdir
#include <sys/types.h>
#include <sys/stat.h>
#else
// posix part

#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <dirent.h>

#ifdef TORRENT_LINUX
// linux specifics

#include <sys/ioctl.h>
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

	int preadv(HANDLE fd, libtorrent::file::iovec_t const* bufs, int num_bufs, libtorrent::size_type file_offset)
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
			file_offset += bufs[i].iov_len;
		}

		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_read;
			if (ReadFile(fd, bufs[i].iov_base, bufs[i].iov_len, &num_read, &ol[i]) == FALSE
				&& GetLastError() != ERROR_IO_PENDING)
			{
				ret = -1;
				goto done;
			}
		}

		WaitForMultipleObjects(num_bufs, h, TRUE, INFINITE);

		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_read;
			if (GetOverlappedResult(fd, &ol[i], &num_read, TRUE) == FALSE)
			{
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

	int pwritev(HANDLE fd, libtorrent::file::iovec_t const* bufs, int num_bufs, libtorrent::size_type file_offset)
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
			file_offset += bufs[i].iov_len;
		}

		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_written;
			if (WriteFile(fd, bufs[i].iov_base, bufs[i].iov_len, &num_written, &ol[i]) == FALSE
				&& GetLastError() != ERROR_IO_PENDING)
			{
				ret = -1;
				goto done;
			}
		}

		WaitForMultipleObjects(num_bufs, h, TRUE, INFINITE);

		for (int i = 0; i < num_bufs; ++i)
		{
			DWORD num_written;
			if (GetOverlappedResult(fd, &ol[i], &num_written, TRUE) == FALSE)
			{
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

#ifdef TORRENT_WINDOWS
#if defined UNICODE && !TORRENT_USE_WSTRING
#warning wide character support not available. Files will be saved using narrow string names
#endif
#endif // TORRENT_WINDOWS

namespace libtorrent
{
#ifdef TORRENT_DISK_STATS
	void write_disk_log(FILE* f, file::aiocb_t const* aio, bool complete, ptime timestamp)
	{
		// the event format in the log is:
		// uint64_t timestamp (microseconds)
		// uint64_t file offset
		// uint32_t file-id
		// uint8_t  event (0: start read, 1: start write, 2: complete read, 4: complete write)
		char event[29];
		char* ptr = event;
		detail::write_uint64(total_microseconds((timestamp - min_time())), ptr);
		detail::write_uint64(aio_offset(aio), ptr);
		detail::write_uint64((boost::uint64_t)aio, ptr);
		detail::write_uint32(aio->file_ptr->file_id(), ptr);
		detail::write_uint8((int(complete) << 1) | (aio_op(aio) == file::write_op), ptr);

		int ret = fwrite(event, 1, sizeof(event), f);
		if (ret != sizeof(event))
		{
			fprintf(stderr, "ERROR writing to disk access log: (%d) %s\n"
				, errno, strerror(errno));
		}
	}
#endif

#ifdef TORRENT_WINDOWS
	std::string convert_separators(std::string p)
	{
		for (int i = 0; i < p.size(); ++i)
			if (p[i] == '/') p[i] = '\\';
		return p;
	}
#endif

	void stat_file(std::string inf, file_status* s
		, error_code& ec, int flags)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
		// apparently windows doesn't expect paths
		// to directories to ever end with a \ or /
		if (!inf.empty() && (inf[inf.size() - 1] == '\\'
			|| inf[inf.size() - 1] == '/'))
			inf.resize(inf.size() - 1);
#endif

#if TORRENT_USE_WSTRING && defined TORRENT_WINDOWS
		std::wstring f = convert_to_wstring(inf);
#else
		std::string f = convert_to_native(inf);
#endif

#ifdef TORRENT_WINDOWS
		struct _stati64 ret;
#if TORRENT_USE_WSTRING
		if (_wstati64(f.c_str(), &ret) < 0)
#else
		if (_stati64(f.c_str(), &ret) < 0)
#endif
		{
			ec.assign(errno, boost::system::get_generic_category());
			return;
		}
#else
		struct stat ret;
		int retval;
		if (flags & dont_follow_links)
			retval = ::lstat(f.c_str(), &ret);
		else
			retval = ::stat(f.c_str(), &ret);
		if (retval < 0)
		{
			ec.assign(errno, boost::system::get_generic_category());
			return;
		}
#endif // TORRENT_WINDOWS

		s->file_size = ret.st_size;
		s->atime = ret.st_atime;
		s->mtime = ret.st_mtime;
		s->ctime = ret.st_ctime;
		s->mode = ret.st_mode;
	}

	void rename(std::string const& inf, std::string const& newf, error_code& ec)
	{
		ec.clear();

#if TORRENT_USE_WSTRING && defined TORRENT_WINDOWS
		std::wstring f1 = convert_to_wstring(inf);
		std::wstring f2 = convert_to_wstring(newf);
		if (_wrename(f1.c_str(), f2.c_str()) < 0)
#else
		std::string f1 = convert_to_native(inf);
		std::string f2 = convert_to_native(newf);
		if (::rename(f1.c_str(), f2.c_str()) < 0)
#endif
		{
			ec.assign(errno, boost::system::get_generic_category());
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
		std::string n = convert_to_native(f);
#endif

#ifdef TORRENT_WINDOWS
		if (CreateDirectory_(n.c_str(), 0) == 0
			&& GetLastError() != ERROR_ALREADY_EXISTS)
			ec.assign(GetLastError(), boost::system::get_system_category());
#else
		int ret = mkdir(n.c_str(), 0777);
		if (ret < 0 && errno != EEXIST)
			ec.assign(errno, boost::system::get_generic_category());
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
		std::string f1 = convert_to_native(inf);
		std::string f2 = convert_to_native(newf);
#endif

#ifdef TORRENT_WINDOWS
		if (CopyFile_(f1.c_str(), f2.c_str(), false) == 0)
			ec.assign(GetLastError(), boost::system::get_system_category());
#elif defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
		// this only works on 10.5
		copyfile_state_t state = copyfile_state_alloc();
		if (copyfile(f1.c_str(), f2.c_str(), state, COPYFILE_ALL) < 0)
			ec.assign(errno, boost::system::get_generic_category());
		copyfile_state_free(state);
#else
		int infd = ::open(inf.c_str(), O_RDONLY);
		if (infd < 0)
		{
			ec.assign(errno, boost::system::get_generic_category());
			return;
		}

		// rely on default umask to filter x and w permissions
		// for group and others
		// TODO: copy the mode from the source file
		int permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		int outfd = ::open(newf.c_str(), O_WRONLY | O_CREAT, permissions);
		if (outfd < 0)
		{
			close(infd);
			ec.assign(errno, boost::system::get_generic_category());
			return;
		}
		char buffer[4096];
		for (;;)
		{
			int num_read = read(infd, buffer, sizeof(buffer));
			if (num_read == 0) break;
			if (num_read < 0)
			{
				ec.assign(errno, boost::system::get_generic_category());
				break;
			}
			int num_written = write(outfd, buffer, num_read);
			if (num_written < num_read)
			{
				ec.assign(errno, boost::system::get_generic_category());
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
#ifdef TORRENT_WINDOWS
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
		char const* ext = strrchr(f.c_str(), '.');
		if (ext == 0) return "";
		return ext;
	}

	void replace_extension(std::string& f, std::string const& ext)
	{
		char const* e = strrchr(f.c_str(), '.');
		if (e == 0) f += '.';
		else f.resize(e - f.c_str() + 1);
		f += ext;
	}

	bool is_root_path(std::string const& f)
	{
		if (f.empty()) return false;

#ifdef TORRENT_WINDOWS
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
			for (int i = 2; i < f.size() - 1; ++i)
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
#ifdef TORRENT_WINDOWS
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
#ifdef TORRENT_WINDOWS
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

	std::string combine_path(std::string const& lhs, std::string const& rhs)
	{
		TORRENT_ASSERT(!is_complete(rhs));
		if (lhs.empty() || lhs == ".") return rhs;
		if (rhs.empty() || rhs == ".") return lhs;

#ifdef TORRENT_WINDOWS
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
#ifdef TORRENT_WINDOWS
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
#if defined TORRENT_WINDOWS && TORRENT_USE_WSTRING
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

	size_type file_size(std::string const& f)
	{
		error_code ec;
		file_status s;
		stat_file(f, &s, ec);
		if (ec) return 0;
		return s.file_size;
	}

	bool exists(std::string const& f)
	{
		error_code ec;
		file_status s;
		stat_file(f, &s, ec);
		if (ec) return false;
		return true;
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
			ec.assign(GetLastError(), boost::system::get_system_category());
			return;
		}
#else // TORRENT_WINDOWS
		std::string f = convert_to_native(inf);
		if (::remove(f.c_str()) < 0)
		{
			ec.assign(errno, boost::system::get_generic_category());
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
		return combine_path(current_working_directory(), f);
	}

	bool is_complete(std::string const& f)
	{
		if (f.empty()) return false;
#ifdef TORRENT_WINDOWS
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
			ec.assign(GetLastError(), boost::system::get_system_category());
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
			ec.assign(errno, boost::system::get_generic_category());
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
				ec.assign(err, boost::system::get_system_category());
		}
#else
		dirent* dummy;
		if (readdir_r(m_handle, &m_dirent, &dummy) != 0)
		{
			ec.assign(errno, boost::system::get_generic_category());
			m_done = true;
		}
		if (dummy == 0) m_done = true;
#endif
	}

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE -1
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

#ifdef TORRENT_DISK_STATS
		m_file_id = silly_hash(path);
#endif

#ifdef TORRENT_WINDOWS

		struct open_mode_t
		{
			DWORD rw_mode;
			DWORD create_mode;
		};

		const static open_mode_t mode_array[] =
		{
			// read_only
			{GENERIC_READ, OPEN_EXISTING},
			// write_only
			{GENERIC_WRITE, OPEN_ALWAYS},
			// read_write
			{GENERIC_WRITE | GENERIC_READ, OPEN_ALWAYS},
		};

		const static DWORD attrib_array[] =
		{
			FILE_ATTRIBUTE_NORMAL, // no attrib
			FILE_ATTRIBUTE_HIDDEN, // hidden
			FILE_ATTRIBUTE_NORMAL, // executable
			FILE_ATTRIBUTE_HIDDEN, // hidden + executable
		};

		const static DWORD share_array[] =
		{
			// read only (no locking)
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			// write only (no locking)
			FILE_SHARE_READ,
			// read/write (no locking)
			FILE_SHARE_READ,
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
			| (a ? a : FILE_ATTRIBUTE_NORMAL) | FILE_FLAG_OVERLAPPED;

		handle_type handle = CreateFile_(m_path.c_str(), m.rw_mode
			, (mode & lock_file) ? 0 : share_array[mode & rw_mask]
			, 0, m.create_mode, flags, 0);

		if (handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), get_system_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		m_file_handle = handle;

		// try to make the file sparse if supported
		// only set this flag if the file is opened for writing
		if ((mode & file::sparse) && (mode & rw_mask) != read_only)
		{
			DWORD temp;
			::DeviceIoControl(native_handle(), FSCTL_SET_SPARSE, 0, 0
				, 0, 0, &temp, 0);
		}
#else // TORRENT_WINDOWS

		// rely on default umask to filter x and w permissions
		// for group and others
		int permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		if (mode & attribute_executable)
			permissions |= S_IXGRP | S_IXOTH | S_IXUSR;

		static const int mode_array[] = {O_RDONLY, O_WRONLY | O_CREAT, O_RDWR | O_CREAT};

#ifdef O_NOATIME
		static const int no_atime_flag[] = {0, O_NOATIME};
#endif

 		handle_type handle = ::open(convert_to_native(path).c_str()
 			, mode_array[mode & rw_mask]
#ifdef O_NOATIME
			| no_atime_flag[(mode & no_atime) >> 4]
#endif
			, permissions);

		if (handle == -1)
		{
			ec.assign(errno, get_posix_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		m_file_handle = handle;

#ifdef F_SETLK
		if (mode & lock_file)
		{
			struct flock l =
			{
				0, // start offset
				0, // length (0 = until EOF)
				getpid(), // owner
				(mode & write_only) ? F_WRLCK : F_RDLCK, // lock type
				SEEK_SET // whence
			};
			if (fcntl(m_file_handle, F_SETLK, &l) != 0)
			{
				ec.assign(errno, get_posix_category());
				return false;
			}
		}
#endif

#ifdef DIRECTIO_ON
		// for solaris
		if (mode & no_cache)
		{
			int yes = 1;
			directio(m_file_handle, DIRECTIO_ON);
		}
#endif

#ifdef F_NOCACHE
		// for BSD/Mac
		if (mode & no_cache)
		{
			int yes = 1;
			fcntl(m_file_handle, F_NOCACHE, &yes);
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
		CloseHandle(m_file_handle);
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
	size_type iov(Fun f, handle_type fd, size_type file_offset, file::iovec_t const* bufs_in
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
				ec.assign(GetLastError(), get_system_category());
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
				ec.assign(GetLastError(), get_system_category());
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

#else

		int ret = 0;

#ifdef TORRENT_WINDOWS
		if (SetFilePointerEx(fd, offs, &offs, FILE_BEGIN) == FALSE)
		{
			ec.assign(GetLastError(), get_system_category());
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
				ec.assign(GetLastError(), get_system_category());
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
	size_type file::readv(size_type file_offset, iovec_t const* bufs, int num_bufs
		, error_code& ec, int flags)
	{
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
	size_type file::writev(size_type file_offset, iovec_t const* bufs, int num_bufs
		, error_code& ec, int flags)
	{
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
		return ret;
	}

	bool file::set_size(size_type s, error_code& ec)
	{
		TORRENT_ASSERT(is_open());
		TORRENT_ASSERT(s >= 0);

#ifdef TORRENT_WINDOWS

		LARGE_INTEGER offs;
		LARGE_INTEGER cur_size;
		if (GetFileSizeEx(native_handle(), &cur_size) == FALSE)
		{
			ec.assign(GetLastError(), get_system_category());
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
				ec.assign(GetLastError(), get_system_category());
				return false;
			}
			if (::SetEndOfFile(native_handle()) == FALSE)
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
				SetFileValidData(native_handle(), offs.QuadPart);
			}
		}
#endif // _WIN32_WINNT >= 0x501
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

	void file::finalize()
	{
#ifdef TORRENT_WINDOWS
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
		::DeviceIoControl(native_handle(), FSCTL_SET_SPARSE, &b, sizeof(b)
			, 0, 0, &temp, 0);
#endif
	}

	size_type file::get_size(error_code& ec) const
	{
#ifdef TORRENT_WINDOWS
		LARGE_INTEGER file_size;
		if (!GetFileSizeEx(native_handle(), &file_size))
		{
			ec.assign(GetLastError(), get_system_category());
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

	size_type file::sparse_end(size_type start) const
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
		size_type file_size = get_size(ec);
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
		size_type ret = lseek(native_handle(), start, SEEK_DATA);
		if (ret < 0) return start;
		return start;
#else
		return start;
#endif
	}

}

