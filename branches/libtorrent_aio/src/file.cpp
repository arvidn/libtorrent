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
#include "libtorrent/aiocb_pool.hpp"

#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>

#define DEBUG_AIO 0

#define DLOG if (DEBUG_AIO) fprintf

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
#include <fcntl.h> // for F_LOG2PHYS
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <dirent.h>

#ifdef TORRENT_LINUX
// linux specifics

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

#elif defined __APPLE__ && defined __MACH__
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

#include "libtorrent/file.hpp"
#include <cstring>
#include <vector>

// for convert_to_wstring and convert_to_native
#include "libtorrent/escape_string.hpp"
#include <stdio.h>
#include "libtorrent/assert.hpp"

#ifdef TORRENT_DEBUG
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::no_buffer) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::rw_mask & libtorrent::file::attribute_mask) == 0);
BOOST_STATIC_ASSERT((libtorrent::file::no_buffer & libtorrent::file::attribute_mask) == 0);
#endif

#ifdef TORRENT_WINDOWS
#if defined UNICODE && !TORRENT_USE_WSTRING
#warning wide character support not available. Files will be saved using narrow string names
#endif
#endif // TORRENT_WINDOWS

namespace libtorrent
{
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
			int num_written = write(outfd, buffer, num_read);
			if (num_written < num_read)
			{
				ec.assign(errno, boost::system::get_generic_category());
				break;
			}
			if (num_read < sizeof(buffer)) break;
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
		if (f == "\\\\") return true;
		int i = 0;
		// match the xx:\ or xx:/ form
		while (f[i] && is_alpha(f[i])) ++i;
		if (i == int(f.size()-2) && f[i] == ':' && (f[i+1] == '\\' || f[i+1] == '/'))
			return true;
#else
		// as well as parent_path("/") should be "/".
		if (f == "/") return true;
#endif
		return false;
	}

	bool has_parent_path(std::string const& f)
	{
		if (f.empty()) return false;

#ifdef TORRENT_WINDOWS
		if (f == "\\\\") return false;
#else
		// as well as parent_path("/") should be "/".
		if (f == "/") return false;
#endif

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

		if (sep - first == f.size() - 1)
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
		if (lhs.empty()) return rhs;
		if (rhs.empty()) return lhs;

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
		std::string f = path;
		if (!f.empty() && (f[f.size()-1] != '/' || f[f.size()-1] != '\\')) f += "\\*";
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
	{}

	file::file(std::string const& path, int mode, error_code& ec)
		: m_file_handle(INVALID_HANDLE_VALUE)
		, m_open_mode(0)
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		, m_sector_size(0)
#endif
	{
		open(path, mode, ec);
	}

	file::~file()
	{
		close();
	}

	bool file::open(std::string const& path, int mode, error_code& ec)
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

#if TORRENT_USE_WSTRING
#define CreateFile_ CreateFileW
		m_path = convert_to_wstring(path);
#else
#define CreateFile_ CreateFileA
		m_path = convert_to_native(path);
#endif

		TORRENT_ASSERT((mode & mode_mask) < sizeof(mode_array)/sizeof(mode_array[0]));
		open_mode_t const& m = mode_array[mode & mode_mask];
		DWORD a = attrib_array[(mode & attribute_mask) >> 12];

		handle_type handle = CreateFile_(m_path.c_str(), m.rw_mode, m.share_mode, 0
			, m.create_mode, m.flags
				| (a ? a : FILE_ATTRIBUTE_NORMAL)
#if TORRENT_USE_OVERLAPPED
				| FILE_FLAG_OVERLAPPED
#endif
				, 0);

		if (handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), get_system_category());
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
#ifdef O_DIRECT
		static const int no_buffer_flag[] = {0, O_DIRECT};
#else
		static const int no_buffer_flag[] = {0, 0};
#endif

#ifdef O_NOATIME
		static const int no_atime_flag[] = {0, O_NOATIME};
#endif

 		handle_type handle = ::open(convert_to_native(path).c_str()
 			, mode_array[mode & rw_mask]
			| no_buffer_flag[(mode & no_buffer) >> 2]
#ifdef O_NOATIME
			| no_atime_flag[(mode & no_atime) >> 4]
#endif
			, permissions);

#ifdef TORRENT_LINUX
		// workaround for linux bug
		// https://bugs.launchpad.net/ubuntu/+source/linux/+bug/269946
		if (handle == -1 && (mode & no_buffer) && errno == EINVAL)
		{
			mode &= ~no_buffer;
			handle = ::open(path.c_str()
				, mode & (rw_mask | no_buffer), permissions);
		}

#endif
		if (handle == -1)
		{
			ec.assign(errno, get_posix_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		m_file_handle = handle;

#ifdef DIRECTIO_ON
		// for solaris
		if (mode & no_buffer)
		{
			int yes = 1;
			directio(native_handle(), DIRECTIO_ON);
		}
#endif

#ifdef F_NOCACHE
		// for BSD/Mac
		if (mode & no_buffer)
		{
			int yes = 1;
			fcntl(native_handle(), F_NOCACHE, &yes);
		}
#endif

#ifdef POSIX_FADV_RANDOM
		// disable read-ahead
		posix_fadvise(native_handle(), 0, 0, POSIX_FADV_RANDOM);
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

	int file::pos_alignment() const
	{
		// on linux and windows, file offsets needs
		// to be aligned to the disk sector size
#if defined TORRENT_LINUX
		if (m_sector_size == 0)
		{
			struct statvfs fs;
			if (fstatvfs(native_handle(), &fs) == 0)
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
#if TORRENT_USE_WSTRING
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
	
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX || defined TORRENT_DEBUG

	int file::m_page_size = 0;

	void file::init_file()
	{
		if (m_page_size != 0) return;

		m_page_size = page_size();
	}

#endif

	file::aiocb_t* file::async_io(size_type offset
		, iovec_t const* bufs, int num_bufs, int op
		, aiocb_pool& pool)
	{
		aiocb_t* ret = 0;
		aiocb_t* prev = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			aiocb_t* aio = pool.construct();
			memset(aio, 0, sizeof(aiocb_t));
#if TORRENT_USE_AIO
			aio->cb.aio_fildes = m_file_handle;
			aio->cb.aio_buf = bufs[i].iov_base;
			aio->cb.aio_nbytes = bufs[i].iov_len;
			aio->cb.aio_offset = offset;
			aio->cb.aio_lio_opcode = op;
#elif TORRENT_USE_OVERLAPPED
			aio->ov.Internal = 0;
			aio->ov.InternalHigh = 0;
			aio->ov.OffsetHigh = DWORD(offset >> 32);
			aio->ov.Offset = DWORD(offset & 0xffffffff);
			aio->ov.hEvent = CreateEvent(0, true, false, 0);
			aio->size = bufs[i].iov_len;
			aio->buf = bufs[i].iov_base;
			aio->phys_offset = phys_offset(offset);
			aio->op = op;
			aio->file = native_handle();
#else
			aio->file_ptr = this;
			aio->buf = bufs[i].iov_base;
			aio->size = bufs[i].iov_len;
			aio->offset = offset;
			aio->op = op;
			aio->phys_offset = phys_offset(offset);
#endif
			// link in to the double linked list
			aio->prev = prev;
			aio->next = 0;
			if (prev) prev->next = aio;
			if (ret == 0) ret = aio;

			offset += bufs[i].iov_len;
			prev = aio;
		}
		TORRENT_ASSERT(ret == 0 || ret->prev == 0);
		return ret;
	}

	file::aiocb_t* file::async_writev(size_type offset
		, iovec_t const* bufs, int num_bufs, aiocb_pool& pool)
	{
		TORRENT_ASSERT((m_open_mode & rw_mask) == write_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(is_open());
		
		return async_io(offset, bufs, num_bufs, write_op, pool);
	}

	file::aiocb_t* file::async_readv(size_type offset
		, iovec_t const* bufs, int num_bufs, aiocb_pool& pool)
	{
		TORRENT_ASSERT((m_open_mode & rw_mask) == read_only || (m_open_mode & rw_mask) == read_write);
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(is_open());

		return async_io(offset, bufs, num_bufs, read_op, pool);
	}

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

#if !TORRENT_USE_OVERLAPPED

			// If we're not using overlapped I/O, we can't use ReadFileScatter
			// anyway. Just loop over the buffers and write them one at a time
   
			LARGE_INTEGER offs;
			offs.QuadPart = file_offset;
			if (SetFilePointerEx(native_handle(), offs, &offs, FILE_BEGIN) == FALSE)
			{
				ec.assign(GetLastError(), get_system_category());
				return -1;
			}
   
			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				DWORD intermediate = 0;
				if (ReadFile(native_handle(), (char*)i->iov_base
					, (DWORD)i->iov_len, &intermediate, 0) == FALSE)
				{
					ec.assign(GetLastError(), get_system_category());
					return -1;
				}
				ret += intermediate;
			}
			return ret;

#endif // TORRENT_USE_OVERLAPPED

			OVERLAPPED* ol = TORRENT_ALLOCA(OVERLAPPED, num_bufs);
			memset(ol, 0, sizeof(OVERLAPPED) * num_bufs);

			int c = 0;
			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i, ++c)
			{
				ol[c].Internal = 0;
				ol[c].InternalHigh = 0;
				ol[c].OffsetHigh = file_offset >> 32;
				ol[c].Offset = file_offset & 0xffffffff;
				ol[c].hEvent = CreateEvent(0, true, false, 0);

				if (ReadFile(native_handle(), (char*)i->iov_base
					, (DWORD)i->iov_len, NULL, &ol[c]) == FALSE)
				{
					if (GetLastError() != ERROR_IO_PENDING)
					{
						TORRENT_ASSERT(GetLastError() != ERROR_INVALID_PARAMETER);
						ec.assign(GetLastError(), get_system_category());
						ret = -1;
						break;
					}
				}
				file_offset += i->iov_len;
			}

			for (int i = 0; i < c; ++i)
			{
				DWORD intermediate = 0;
				if (GetOverlappedResult(native_handle(), &ol[i], &intermediate, true) == 0)
				{
					ec.assign(GetLastError(), get_system_category());
					ret = -1;
				}
				CloseHandle(ol[i].hEvent);
				if (ret != -1) ret += intermediate;
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
			for (int k = 0; k < int(i->iov_len); k += m_page_size)
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
		ol.OffsetHigh = DWORD(file_offset >> 32);
		ol.Offset = DWORD(file_offset & 0xffffffff);
		ol.hEvent = CreateEvent(0, true, false, 0);

		ret += size;
		size = num_pages * m_page_size;
		if (ReadFileScatter(native_handle(), segment_array, size, 0, &ol) == 0)
		{
			DWORD last_error = GetLastError();
			if (last_error != ERROR_IO_PENDING)
			{
				TORRENT_ASSERT(GetLastError() != ERROR_INVALID_PARAMETER);
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (GetOverlappedResult(native_handle(), &ol, &ret, true) == 0)
			{
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
		}
		CloseHandle(ol.hEvent);
		return ret;

#else // TORRENT_WINDOWS

		size_type ret = lseek(native_handle(), file_offset, SEEK_SET);
		if (ret < 0)
		{
			ec.assign(errno, get_posix_category());
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
			if ((size & (size_alignment()-1)) == 0) aligned = true;
		}
		if (aligned)
#endif // TORRENT_LINUX
		{
			ret = ::readv(native_handle(), bufs, num_bufs);
			if (ret < 0)
			{
				ec.assign(errno, get_posix_category());
				return -1;
			}
			return ret;
		}
#ifdef TORRENT_LINUX
		file::iovec_t* temp_bufs = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		memcpy(temp_bufs, bufs, sizeof(file::iovec_t) * num_bufs);
		iovec_t& last = temp_bufs[num_bufs-1];
		last.iov_len = (last.iov_len & ~(size_alignment()-1)) + m_page_size;
		ret = ::readv(native_handle(), temp_bufs, num_bufs);
		if (ret < 0)
		{
			ec.assign(errno, get_posix_category());
			return -1;
		}
		return (std::min)(ret, size_type(size));
#endif // TORRENT_LINUX

#else // TORRENT_USE_READV

		ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int tmp = read(native_handle(), i->iov_base, i->iov_len);
			if (tmp < 0)
			{
				ec.assign(errno, get_posix_category());
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

#if !TORRENT_USE_OVERLAPPED

			// If we're not using overlapped I/O at all. We can't use
			// WriteFileGather anyway. Just loop over the buffers and
			// write them one at a time
   
			LARGE_INTEGER offs;
			offs.QuadPart = file_offset;
			if (SetFilePointerEx(native_handle(), offs, &offs, FILE_BEGIN) == FALSE)
			{
				ec.assign(GetLastError(), get_system_category());
				return -1;
			}
   
			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				DWORD intermediate = 0;
				if (WriteFile(native_handle(), (char const*)i->iov_base
					, (DWORD)i->iov_len, &intermediate, 0) == FALSE)
				{
					ec.assign(GetLastError(), get_system_category());
					return -1;
				}
				ret += intermediate;
			}
			return ret;

#endif // TORRENT_USE_OVERLAPPED

			OVERLAPPED* ol = TORRENT_ALLOCA(OVERLAPPED, num_bufs);
			memset(ol, 0, sizeof(OVERLAPPED) * num_bufs);

			int c = 0;
			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i, ++c)
			{
				ol[c].Internal = 0;
				ol[c].InternalHigh = 0;
				ol[c].OffsetHigh = file_offset >> 32;
				ol[c].Offset = file_offset & 0xffffffff;
				ol[c].hEvent = CreateEvent(0, true, false, 0);

				if (WriteFile(native_handle(), (char const*)i->iov_base
					, (DWORD)i->iov_len, NULL, &ol[c]) == FALSE)
				{
					if (GetLastError() != ERROR_IO_PENDING)
					{
						TORRENT_ASSERT(GetLastError() != ERROR_INVALID_PARAMETER);
						ec.assign(GetLastError(), get_system_category());
						ret = -1;
						break;
					}
				}
				file_offset += i->iov_len;
			}

			for (int i = 0; i < c; ++i)
			{
				DWORD intermediate = 0;
				if (GetOverlappedResult(native_handle(), &ol[i], &intermediate, true) == 0)
				{
					ec.assign(GetLastError(), get_system_category());
					ret = -1;
				}
				CloseHandle(ol[i].hEvent);
				if (ret != -1) ret += intermediate;
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
			for (int k = 0; k < int(i->iov_len); k += m_page_size)
			{
				cur_seg->Buffer = PtrToPtr64((((char*)i->iov_base) + k));
				++cur_seg;
			}
		}
		// terminate the array
		cur_seg->Buffer = 0;

		TORRENT_ASSERT((file_offset % pos_alignment()) == 0);
		OVERLAPPED ol;
		ol.Internal = 0;
		ol.InternalHigh = 0;
		ol.OffsetHigh = DWORD(file_offset >> 32);
		ol.Offset = DWORD(file_offset & 0xffffffff);
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

		if (WriteFileGather(native_handle(), segment_array, size, NULL, &ol) == 0)
		{
			if (GetLastError() != ERROR_IO_PENDING)
			{
				TORRENT_ASSERT(GetLastError() != ERROR_INVALID_PARAMETER);
				TORRENT_ASSERT(GetLastError() != ERROR_BAD_ARGUMENTS);
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			DWORD tmp;
			if (GetOverlappedResult(native_handle(), &ol, &tmp, true) == 0)
			{
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (tmp < ret) ret = tmp;
		}
		CloseHandle(ol.hEvent);

		if (file_size > 0)
		{
			HANDLE f = CreateFile_(m_path.c_str(), GENERIC_WRITE
			, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING
			, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);

			if (f == INVALID_HANDLE_VALUE)
			{
				ec.assign(GetLastError(), get_system_category());
				return -1;
			}

			LARGE_INTEGER offs;
			offs.QuadPart = file_size;
			if (SetFilePointerEx(f, offs, &offs, FILE_BEGIN) == FALSE)
			{
				CloseHandle(f);
				ec.assign(GetLastError(), get_system_category());
				return -1;
			}
			if (::SetEndOfFile(f) == FALSE)
			{
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(f);
				return -1;
			}
			CloseHandle(f);
		}

		return ret;
#else
		size_type ret = lseek(native_handle(), file_offset, SEEK_SET);
		if (ret < 0)
		{
			ec.assign(errno, get_posix_category());
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
			if ((size & (size_alignment()-1)) == 0) aligned = true;
		}
		if (aligned)
#endif
		{
			ret = ::writev(native_handle(), bufs, num_bufs);
			if (ret < 0)
			{
				ec.assign(errno, get_posix_category());
				return -1;
			}
			return ret;
		}
#ifdef TORRENT_LINUX
		file::iovec_t* temp_bufs = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		memcpy(temp_bufs, bufs, sizeof(file::iovec_t) * num_bufs);
		iovec_t& last = temp_bufs[num_bufs-1];
		last.iov_len = (last.iov_len & ~(size_alignment()-1)) + size_alignment();
		ret = ::writev(native_handle(), temp_bufs, num_bufs);
		if (ret < 0)
		{
			ec.assign(errno, get_posix_category());
			return -1;
		}
		if (ftruncate(native_handle(), file_offset + size) < 0)
		{
			ec.assign(errno, get_posix_category());
			return -1;
		}
		return (std::min)(ret, size_type(size));
#endif // TORRENT_LINUX

#else // TORRENT_USE_WRITEV

		ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int tmp = write(native_handle(), i->iov_base, i->iov_len);
			if (tmp < 0)
			{
				ec.assign(errno, get_posix_category());
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

		if (ioctl(native_handle(), FS_IOC_FIEMAP, &fm) == -1)
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
		size_type ret = lseek(native_handle(), offset, SEEK_SET);
		if (ret < 0) return 0;
		if (fcntl(native_handle(), F_LOG2PHYS, &l) == -1) return 0;
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
		int cluster_offset = int(in.StartingVcn.QuadPart % m_cluster_size);

		if (DeviceIoControl(native_handle(), FSCTL_GET_RETRIEVAL_POINTERS, &in
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
				ec.assign(errno, get_posix_category());
				return false;
			}
#endif // F_PREALLOCATE

			int ret;
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
		size_type ret = lseek(native_handle(), start, SEEK_DATA);
		if (ret < 0) return start;
#else
		return start;
#endif
	}

	file::aiocb_t* reap_aios(file::aiocb_t* aios, aiocb_pool& pool)
	{
		file::aiocb_t* ret = aios;
		// loop through all aiocb structures, for operations
		// that are still in progress, add them to a new chain
		// which is returned. For operations that are complete,
		// call the handler and delete aiocb entry.
		while (aios)
		{
			TORRENT_ASSERT(aios->next == 0 || aios->next->prev == aios);
			file::aiocb_t* a = aios;
			aios = aios->next;
			bool removed = reap_aio(a, pool);
			if (removed && ret == a) ret = aios;
		}
		TORRENT_ASSERT(ret == 0 || ret->prev == 0);
		return ret;
	}

	bool reap_aio(file::aiocb_t* aio, aiocb_pool& pool)
	{
#if TORRENT_USE_AIO
		int e = aio_error(&aio->cb);
		if (e == EINPROGRESS) return false;

		size_t ret = aio_return(&aio->cb);
		DLOG(stderr, "aio_return(%p) = %d\n", &aio->cb, int(ret));
		if (ret == -1) DLOG(stderr, " error: %s\n", strerror(errno));
		aio->handler->done(error_code(e, boost::system::get_posix_category()), ret);
#elif TORRENT_USE_OVERLAPPED
		BOOL ret = HasOverlappedIoCompleted(&aio->ov);
		if (ret == FALSE) return false;

		DWORD transferred = 0;
		ret = GetOverlappedResult(aio->file, &aio->ov, &transferred, TRUE);
		DLOG(stderr, "GetOverlappedResul(%p) = %d\n", &aio->ov, int(ret));
		int error = ERROR_SUCCESS;
		if (ret == FALSE)
		{
			error = GetLastError();
			DLOG(stderr, " error: %d\n", error);
		}
		aio->handler->done(error_code(error, boost::system::get_system_category()), transferred);
		CloseHandle(aio->ov.hEvent);
#else
		// since we don't have AIO, all operations should have
		// been performed as they were issued, and no operation
		// should ever be outstanding
		TORRENT_ASSERT(aios == 0);
		return false;
#endif

		// unlink
		if (aio->prev) aio->prev->next = aio->next;
		if (aio->next) aio->next->prev = aio->prev;

		pool.destroy(aio);
		return true;
	}

	std::pair<file::aiocb_t*, file::aiocb_t*> issue_aios(file::aiocb_t* aios
		, aiocb_pool& pool, int& num_issued)
	{
		// this is the chain of aios that were
		// successfully submitted
		file::aiocb_t* ret = aios;

		while (aios)
		{
			TORRENT_ASSERT(aios->next == 0 || aios->next->prev == aios);
#if TORRENT_USE_AIO
			memset(&aios->cb.aio_sigevent, 0, sizeof(aios->cb.aio_sigevent));
			aios->cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
			aios->cb.aio_sigevent.sigev_signo = TORRENT_AIO_SIGNAL;
			int ret;
			switch (aios->cb.aio_lio_opcode)
			{
				case file::read_op: ret = aio_read(&aios->cb); break;
				case file::write_op: ret = aio_write(&aios->cb); break;
				default: TORRENT_ASSERT(false);
			}
			DLOG(stderr, " aio_%s() = %d\n", aios->cb.aio_lio_opcode == file::read_op
				? "read" : "write", ret);

			if (ret == -1)
			{
				DLOG(stderr, "  error = %s\n", strerror(errno));
				if (errno == EAGAIN) break;
			
				// report this error immediately and unlink this job
				if (aios->prev) aios->prev->next = aios->next;
				if (aios->next) aios->next->prev = aios->prev;
				file::aiocb_t* del = aios;
				aios = aios->next;
				del->handler->done(error_code(errno, boost::system::get_posix_category()), ret);
				pool.destroy(del);
				continue;
			}
#elif TORRENT_USE_OVERLAPPED
			extern void WINAPI signal_handler(DWORD, DWORD, OVERLAPPED*);

			// TODO: make the aiocb_t contain the vector of buffers
			// on windows, to allow for issuing a single async operation
			int ret;
			switch (aios->op)
			{
				case file::read_op:
					ret = ReadFile(aios->file, aios->buf
						, aios->size, NULL, &aios->ov);
					break;
				case file::write_op:
					ret = WriteFile(aios->file, aios->buf
						, aios->size, NULL, &aios->ov);
					break;
				default: TORRENT_ASSERT(false);
			}
			DLOG(stderr, " %sFile() = %d\n", aios->op == file::read_op
				? "Read" : "Write", ret);

			if (ret == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				DWORD error = GetLastError();
				DLOG(stderr, "  error = %d\n", error);

				// report this error immediately and unlink this job
				if (aios->prev) aios->prev->next = aios->next;
				if (aios->next) aios->next->prev = aios->prev;
				file::aiocb_t* del = aios;
				aios = aios->next;
				del->handler->done(error_code(errno, boost::system::get_posix_category()), ret);
				pool.destroy(del);
				continue;
			}
#else
			error_code ec;
			int ret;
			// #error merge adjacent operations into a vector call
			file::iovec_t b = {aios->buf, aios->size};
			switch (aios->op)
			{
				case file::read_op: ret = aios->file_ptr->readv(aios->offset, &b, 1, ec); break;
				case file::write_op: ret = aios->file_ptr->writev(aios->offset, &b, 1, ec); break;
				default: TORRENT_ASSERT(false);
			}
			file::aiocb_t* del = aios;
			if (aios->next) aios->next->prev = aios->prev;
			if (aios->prev) aios->prev->next = aios->next;
			aios = aios->next;
			del->handler->done(ec, ret);
			pool.destroy(del);
			++num_issued;
			return std::pair<file::aiocb_t*, file::aiocb_t*>(0, aios);
#endif
		
			++num_issued;
			aios = aios->next;
		}

		if (aios)
		{
			// this job, and all following it, could not be submitted
			// cut the chain in two, one for the ones that were submitted
			// and one for the ones that are left
			if (aios->prev)
			{
				aios->prev->next = 0;
			}
			else
			{
				TORRENT_ASSERT(ret == aios);
				ret = 0;
			}
			aios->prev = 0;
		}

		TORRENT_ASSERT(ret == 0 || ret->prev == 0);
		TORRENT_ASSERT(aios == 0 || aios->prev == 0);
		return std::pair<file::aiocb_t*, file::aiocb_t*>(ret, aios);
	}

#if 0
	std::pair<file::aiocb_t*, file::aiocb_t*> issue_aios(file::aiocb_t* aios
		, aiocb_pool& pool, int& num_issued)
	{
#if TORRENT_USE_AIO
		// this code uses lio_listio() to save system calls
#if 0
		// figure out how many requests we can issue at a time
		int sc = sysconf(_SC_AIO_LISTIO_MAX);
		if (sc == -1 && errno == 0)
		{
			// unlimited
			sc = INT_MAX;
		}
		else if (sc == -1)
		{
			// something wrong, fall back to constant
#ifdef AIO_LISTIO_MAX
			sc = AIO_LISTIO_MAX;
#else
			// we are guaranteed that we can at least issue 2 requests
			sc = 2;
#endif
		}
#ifdef AIO_LISTIO_MAX
		// apparently Mac OS will fail if we pass in more
		// than the constant, and the sysconf() returns
		// the current limit of outstanding requests (which
		// seems to contradict the documentation for the sysconf
		// variable).
		sc = (std::min)(AIO_LISTIO_MAX, sc);
#endif
		const int array_size = sc < 100 ? sc : 100;
		aiocb** array = TORRENT_ALLOCA(aiocb*, array_size);
		if (array == 0)
		{
			TORRENT_ASSERT(false);
			return std::pair<file::aiocb_t*, file::aiocb_t*>(0, aios);
		}
		sigevent sig;
		memset(&sig, 0, sizeof(sig));
		sig.sigev_notify = SIGEV_SIGNAL;
		sig.sigev_signo = TORRENT_AIO_SIGNAL;

		// this is the first aio in the array
		// we're currently submitting
		file::aiocb_t* list_start = aios;
		// this is the chain of aios that were
		// successfully submitted
		file::aiocb_t* ret = 0;
		// this is the pointer to the last
		// chain link of the ret chain, for
		// easy appending to it
		file::aiocb_t** ret_last = &ret;
		int i = 0;
		for (;;)
		{
			if (i == array_size || aios == 0)
			{
				int ret = lio_listio(LIO_NOWAIT, array, i, &sig);
				DLOG(stderr, "lio_listio(%d) = %d\n", i, ret);
				if (ret == -1) DLOG(stderr, "  error: %s\n", strerror(errno));
				if (ret == -1)
				{
					// we need to iterate over all aiocb's to know which
					// ones were actually started
					file::aiocb_t** prev_link = 0;
					for (file::aiocb_t* j = list_start; j != aios; j = j->next)
					{
						ret = aio_error(&j->cb);
						DLOG(stderr, "aio_error(%p) = %s\n", &j->cb, strerror(ret));
						if (ret == -1) DLOG(stderr, "  error: %s\n", strerror(errno));
						if (ret != -1)
						{
							++num_issued;
							// yes, this one was actually added. unlink it from
							// the chain and stick it in the return chain

							// unlink
							if (prev_link) *prev_link = j->next;

							// if this is the first element in the remaining
							// list, push the start link out one step
							if (j == list_start) list_start = j->next;

							// link in to return list
							*ret_last = j;
							ret_last = &j->next;
						}
						else
						{
							// nope, this was not added
							if (prev_link) *prev_link = j;

							// update the last entry in the remaining list
							prev_link = &j->next;
						}
					}
					if (prev_link) *prev_link = aios;
					*ret_last = 0;
					i = 0;
					break;
				}
				// move the aiocb_t entries over to the ret chain
				while (list_start != aios)
				{
					++num_issued;
					*ret_last = list_start;
					ret_last = &list_start->next;
					list_start = list_start->next;
				}
				// terminate the ret chain
				*ret_last = 0;

				i = 0;
			}
			if (aios == 0) break;

			aios->cb.aio_sigevent = sig;
			array[i] = &aios->cb;
			aios = aios->next;
			++i;
		}

		return std::pair<file::aiocb_t*, file::aiocb_t*>(ret, list_start);
#else

		// this is the chain of aios that were
		// successfully submitted
		file::aiocb_t* ret = aios;
		// this is the pointer to the last
		// chain link of the ret chain, for
		// easy appending to it
		file::aiocb_t** last = &ret;

		while (aios)
		{
			memset(&aios->cb.aio_sigevent, 0, sizeof(aios->cb.aio_sigevent));
			aios->cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
			aios->cb.aio_sigevent.sigev_signo = TORRENT_AIO_SIGNAL;
			int ret;
			switch (aios->cb.aio_lio_opcode)
			{
				case file::read_op: ret = aio_read(&aios->cb); break;
				case file::write_op: ret = aio_write(&aios->cb); break;
				default: TORRENT_ASSERT(false);
			}
			DLOG(stderr, " aio_%s() = %d\n", aios->cb.aio_lio_opcode == file::read_op
				? "read" : "write", ret);

			if (ret == -1)
			{
				DLOG(stderr, "  error = %s\n", strerror(errno));
				if (errno == EAGAIN) break;
			
				// report this error immediately and unlink this job
				aios->handler->done(error_code(errno, boost::system::get_posix_category()), ret);
				*last = aios->next;
				file::aiocb_t* del = aios;
				aios = aios->next;
				pool.destroy(del);
				continue;
			}
		
			++num_issued;
			last = &aios->next;
			aios = aios->next;
		}

		// cut the chain in two. One for successfully
		// submitted jobs, and one with remaining jobs
		*last = 0;

		return std::pair<file::aiocb_t*, file::aiocb_t*>(ret, aios);
#endif

#elif TORRENT_USE_OVERLAPPED

		// this is the chain of aios that were
		// successfully submitted
		file::aiocb_t* ret = aios;
		// this is the pointer to the last
		// chain link of the ret chain, for
		// easy appending to it
		file::aiocb_t** last = &ret;

		extern void WINAPI signal_handler(DWORD, DWORD, OVERLAPPED*);

		while (aios)
		{
			int ret;
			switch (aios->op)
			{
				case file::read_op:
					ret = ReadFileEx(aios->file, aios->buf
						, aios->size, &aios->ov
						, &signal_handler);
					break;
				case file::write_op:
					ret = WriteFileEx(aios->file, aios->buf
						, aios->size, &aios->ov
						, &signal_handler);
					break;
				default: TORRENT_ASSERT(false);
			}
			DLOG(stderr, " %sFile() = %d\n", aios->op == file::read_op
				? "Read" : "Write", ret);

			if (ret == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				DWORD error = GetLastError();
				DLOG(stderr, "  error = %d\n", error);
				// report immediately and unlink this job
				aios->handler->done(error_code(error, boost::system::get_system_category()), -1);
				*last = aios->next;
				file::aiocb_t* del = aios;
				aios = aios->next;
				pool.destroy(del);
				continue;
			}
		
			++num_issued;
			last = &aios->next;
			aios = aios->next;
		}

		// cut the chain in two. One for successfully
		// submitted jobs, and one with remaining jobs
		*last = 0;

		return std::pair<file::aiocb_t*, file::aiocb_t*>(ret, aios);
#else

		error_code ec;
		int ret;
		// #error merge adjacent operations into a vector call
		if (aios)
		{
			file::iovec_t b = {aios->buf, aios->size};
			switch (aios->op)
			{
				case file::read_op: ret = aios->file_ptr->readv(aios->offset, &b, 1, ec); break;
				case file::write_op: ret = aios->file_ptr->writev(aios->offset, &b, 1, ec); break;
				default: TORRENT_ASSERT(false);
			}
			aios->handler->done(ec, ret);
			file::aiocb_t* del = aios;
			aios = aios->next;
			pool.destroy(del);
			++num_issued;
		}
		return std::pair<file::aiocb_t*, file::aiocb_t*>(0, aios);
#endif
	}

#endif

}

