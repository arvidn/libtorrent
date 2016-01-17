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
#include "libtorrent/escape_string.hpp" // for string conversion

#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>

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
#include <fcntl.h> // for F_LOG2PHYS
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
#include <linux/types.h>
#ifdef HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h> // FIEMAP_*
#include <linux/fs.h>  // FS_IOC_FIEMAP
#endif

#ifdef TORRENT_ANDROID
#include <sys/syscall.h>
#define lseek lseek64
#endif

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

	void stat_file(std::string inf, file_status* s
		, error_code& ec, int flags)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS

		std::string p = convert_separators(inf);
#if TORRENT_USE_UNC_PATHS
		// UNC paths must be absolute
		// network paths are already UNC paths
		if (inf.substr(0,2) == "\\\\") p = inf;
		else p = "\\\\?\\" + (is_complete(p) ? p : combine_path(current_working_directory(), p));
#endif

#if TORRENT_USE_WSTRING
#define CreateFile_ CreateFileW
		std::wstring f = convert_to_wstring(p);
#else
#define CreateFile_ CreateFileA
		std::string f = convert_to_native(p);
#endif

		// in order to open a directory, we need the FILE_FLAG_BACKUP_SEMANTICS
		HANDLE h = CreateFile_(f.c_str(), 0, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

		if (h == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), get_system_category());
			TORRENT_ASSERT(ec);
			return;
		}

		BY_HANDLE_FILE_INFORMATION data;
		if (!GetFileInformationByHandle(h, &data))
		{
			ec.assign(GetLastError(), get_system_category());
			TORRENT_ASSERT(ec);
			CloseHandle(h);
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
		CloseHandle(h);
#else
		
		// posix version

		std::string f = convert_to_native(inf);

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
		std::string f1 = convert_to_native(inf);
		std::string f2 = convert_to_native(newf);
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
		std::string n = convert_to_native(f);
#endif

#ifdef TORRENT_WINDOWS
		if (CreateDirectory_(n.c_str(), 0) == 0
			&& GetLastError() != ERROR_ALREADY_EXISTS)
			ec.assign(GetLastError(), get_system_category());
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
		std::string f1 = convert_to_native(inf);
		std::string f2 = convert_to_native(newf);
#endif

#ifdef TORRENT_WINDOWS
		if (CopyFile_(f1.c_str(), f2.c_str(), false) == 0)
			ec.assign(GetLastError(), get_system_category());
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
			ec.assign(GetLastError(), get_system_category());
			return;
		}
#else // TORRENT_WINDOWS
		std::string f = convert_to_native(inf);
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
				ec.assign(err, boost::system::get_system_category());
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
				ec.assign(GetLastError(), get_system_category());
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
					ec.assign(last_error, get_system_category());
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

	file::file(std::string const& path, int mode, error_code& ec)
#ifdef TORRENT_WINDOWS
		: m_file_handle(INVALID_HANDLE_VALUE)
#else
		: m_fd(-1)
#endif
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

	bool file::open(std::string const& path, int mode, error_code& ec)
	{
		close();
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

		DWORD flags
			= ((mode & random_access) ? 0 : FILE_FLAG_SEQUENTIAL_SCAN)
			| (a ? a : FILE_ATTRIBUTE_NORMAL)
			| ((mode & no_buffer) ? FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING : 0);

		m_file_handle = CreateFile_(m_path.c_str(), m.rw_mode
			, (mode & lock_file) ? 0 : share_array[mode & rw_mask]
			, 0, m.create_mode, flags, 0);

		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
			ec.assign(GetLastError(), get_system_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		// try to make the file sparse if supported
		// only set this flag if the file is opened for writing
		if ((mode & file::sparse) && (mode & rw_mask) != read_only)
		{
			DWORD temp;
			bool use_overlapped = (m_open_mode & no_buffer) != 0;
			overlapped_t ol;
			BOOL ret = ::DeviceIoControl(m_file_handle, FSCTL_SET_SPARSE, 0, 0
				, 0, 0, &temp, use_overlapped ? &ol.ol : NULL);
			error_code error;
			if (use_overlapped && ret == FALSE && GetLastError() == ERROR_IO_PENDING)
				ol.wait(m_file_handle, error);
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
#ifdef O_DIRECT
		static const int no_buffer_flag[] = {0, O_DIRECT};
#else
		static const int no_buffer_flag[] = {0, 0};
#endif

#ifdef O_NOATIME
		static const int no_atime_flag[] = {0, O_NOATIME};
#endif

 		m_fd = ::open(convert_to_native(path).c_str()
 			, mode_array[mode & rw_mask]
			| no_buffer_flag[(mode & no_buffer) >> 2]
#ifdef O_NOATIME
			| no_atime_flag[(mode & no_atime) >> 4]
#endif
			, permissions);

#ifdef TORRENT_LINUX
		// workaround for linux bug
		// https://bugs.launchpad.net/ubuntu/+source/linux/+bug/269946
		if (m_fd == -1 && (mode & no_buffer) && errno == EINVAL)
		{
			mode &= ~no_buffer;
			m_fd = ::open(path.c_str()
				, mode_array[mode & rw_mask]
#ifdef O_NOATIME
				| no_atime_flag[(mode & no_atime) >> 4]
#endif

				, permissions);
		}
#endif

#ifdef O_NOATIME
		// O_NOATIME is not allowed for files we don't own
		// so, if we get EPERM when we try to open with it
		// try again without O_NOATIME
		if (m_fd == -1 && (mode & no_atime) && errno == EPERM)
		{
			mode &= ~no_atime;
			m_fd = ::open(path.c_str()
				, mode_array[mode & rw_mask]
				| no_buffer_flag[(mode & no_buffer) >> 2]
				, permissions);
		}
#endif
		if (m_fd == -1)
		{
			ec.assign(errno, get_posix_category());
			TORRENT_ASSERT(ec);
			return false;
		}

		// The purpose of the lock_file flag is primarily to prevent other
		// processes from corrupting files that are being used by libtorrent.
		// the posix file locking mechanism does not prevent others from
		// accessing files, unless they also attempt to lock the file. That's
		// why the SETLK mechanism is not used here.

#ifdef DIRECTIO_ON
		// for solaris
		if (mode & no_buffer)
		{
			int yes = 1;
			directio(m_fd, DIRECTIO_ON);
		}
#endif

#ifdef F_NOCACHE
		// for BSD/Mac
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

#ifdef TORRENT_WINDOWS
	bool is_sparse(HANDLE file, bool overlapped)
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
			, out, sizeof(out), &returned_bytes, overlapped ? &ol.ol : NULL);

		if (overlapped && ret == FALSE && GetLastError() == ERROR_IO_PENDING)
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
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		m_sector_size = 0;
#endif

#ifdef TORRENT_WINDOWS
		if (m_file_handle == INVALID_HANDLE_VALUE) return;

		// if this file is open for writing, has the sparse
		// flag set, but there are no sparse regions, unset
		// the flag
		int rw_mode = m_open_mode & rw_mask;
		bool use_overlapped = (m_open_mode & no_buffer) != 0;
		if ((rw_mode != read_only)
			&& (m_open_mode & sparse)
			&& !is_sparse(m_file_handle, use_overlapped))
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
			BOOL ret = ::DeviceIoControl(m_file_handle, FSCTL_SET_SPARSE, &b, sizeof(b)
				, 0, 0, &temp, use_overlapped ? &ol.ol : NULL);
			error_code ec;
			if (use_overlapped && ret == FALSE && GetLastError() == ERROR_IO_PENDING)
			{
				ol.wait(m_file_handle, ec);
			}
		}

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

	void file::hint_read(size_type file_offset, int len)
	{
#if defined POSIX_FADV_WILLNEED
		posix_fadvise(m_fd, file_offset, len, POSIX_FADV_WILLNEED);
#elif defined F_RDADVISE
		radvisory r;
		r.ra_offset = file_offset;
		r.ra_count = len;
		fcntl(m_fd, F_RDADVISE, &r);
#else
		// TODO: is there any way to pre-fetch data from a file on windows?
#endif
	}

	size_type file::readv(size_type file_offset, iovec_t const* bufs, int num_bufs, error_code& ec)
	{
#ifdef TORRENT_WINDOWS
		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
			ec = error_code(ERROR_INVALID_HANDLE, get_system_category());
			return -1;
		}
#else
		if (m_fd == -1)
		{
			ec = error_code(EBADF, get_system_category());
			return -1;
		}
#endif
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
				ec.assign(GetLastError(), get_system_category());
				return -1;
			}

			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				DWORD intermediate = 0;
				if (ReadFile(m_file_handle, (char*)i->iov_base
					, (DWORD)i->iov_len, &intermediate, 0) == FALSE)
				{
					ec.assign(GetLastError(), get_system_category());
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
#ifdef __GNUC__
		// MingW seems to have issues with 64 bit wide pointers
		// (PVOID64) and only assign the low 32 bits. Therefore, make
		// sure the other 32 bits are cleared out
		memset(segment_array, 0, (num_pages + 1) * sizeof(FILE_SEGMENT_ELEMENT));
#endif
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
		memset(&ol, 0, sizeof(ol));
		ol.Internal = 0;
		ol.InternalHigh = 0;
		ol.OffsetHigh = DWORD(file_offset >> 32);
		ol.Offset = DWORD(file_offset & 0xffffffff);
		ol.hEvent = CreateEvent(0, true, false, 0);
		if (ol.hEvent == NULL)
		{
			ec.assign(GetLastError(), get_system_category());
			return -1;
		}

		ret += size;
		size = num_pages * m_page_size;
		if (ReadFileScatter(m_file_handle, segment_array, size, 0, &ol) == 0)
		{
			DWORD last_error = GetLastError();
			if (last_error != ERROR_IO_PENDING
#ifdef ERROR_CANT_WAIT
				&& last_error != ERROR_CANT_WAIT
#endif
)
			{
				ec.assign(last_error, get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (WaitForSingleObject(ol.hEvent, INFINITE) == WAIT_FAILED)
			{
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			DWORD num_read;
			if (GetOverlappedResult(m_file_handle, &ol, &num_read, false) == 0)
			{
				DWORD last_error = GetLastError();
				if (last_error != ERROR_HANDLE_EOF)
				{
#ifdef ERROR_CANT_WAIT
					TORRENT_ASSERT(last_error != ERROR_CANT_WAIT);
#endif
					ec.assign(last_error, get_system_category());
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
			ec.assign(errno, get_posix_category());
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
					ec.assign(errno, get_posix_category());
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
					ec.assign(errno, get_posix_category());
					return -1;
				}
				ret += (std::min)(tmp_ret, size);
			}
#endif // TORRENT_LINUX

			// returning 0 means end of file. Terminate the loop, there's no
			// point in trying to read more
			if (tmp_ret == 0) break;

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
#ifdef TORRENT_WINDOWS
		if (m_file_handle == INVALID_HANDLE_VALUE)
		{
			ec = error_code(ERROR_INVALID_HANDLE, get_system_category());
			return -1;
		}
#else
		if (m_fd == -1)
		{
			ec = error_code(EBADF, get_system_category());
			return -1;
		}
#endif
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
				ec.assign(GetLastError(), get_system_category());
				return -1;
			}

			for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			{
				DWORD intermediate = 0;
				if (WriteFile(m_file_handle, (char const*)i->iov_base
					, (DWORD)i->iov_len, &intermediate, 0) == FALSE)
				{
					ec.assign(GetLastError(), get_system_category());
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
#ifdef __GNUC__
		// MingW seems to have issues with 64 bit wide pointers
		// (PVOID64) and only assign the low 32 bits. Therefore, make
		// sure the other 32 bits are cleared out
		memset(segment_array, 0, (num_pages + 1) * sizeof(FILE_SEGMENT_ELEMENT));
#endif
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
		memset(&ol, 0, sizeof(ol));
		ol.Internal = 0;
		ol.InternalHigh = 0;
		ol.OffsetHigh = DWORD(file_offset >> 32);
		ol.Offset = DWORD(file_offset & 0xffffffff);
		ol.hEvent = CreateEvent(0, true, false, 0);
		if (ol.hEvent == NULL)
		{
			ec.assign(GetLastError(), get_system_category());
			return -1;
		}

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
			DWORD last_error = GetLastError();
			if (last_error != ERROR_IO_PENDING
#ifdef ERROR_CANT_WAIT
				&& last_error != ERROR_CANT_WAIT
#endif
				)
			{
				TORRENT_ASSERT(last_error != ERROR_BAD_ARGUMENTS);
				ec.assign(last_error, get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			if (WaitForSingleObject(ol.hEvent, INFINITE) == WAIT_FAILED)
			{
				ec.assign(GetLastError(), get_system_category());
				CloseHandle(ol.hEvent);
				return -1;
			}
			DWORD num_written;
			if (GetOverlappedResult(m_file_handle, &ol, &num_written, false) == 0)
			{
				DWORD last_error = GetLastError();
#ifdef ERROR_CANT_WAIT
				TORRENT_ASSERT(last_error != ERROR_CANT_WAIT);
#endif
				ec.assign(last_error, get_system_category());
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
			ec.assign(errno, get_posix_category());
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
					ec.assign(errno, get_posix_category());
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
					ec.assign(errno, get_posix_category());
					return -1;
				}
				if (ftruncate(m_fd, file_offset + size) < 0)
				{
					ec.assign(errno, get_posix_category());
					return -1;
				}
				ret += (std::min)(tmp_ret, size);
			}
#endif // TORRENT_LINUX

			// returning 0 means end of file. Terminate the loop, there's no
			// point in trying to read more
			if (tmp_ret == 0) break;

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

		if (ioctl(m_fd, FS_IOC_FIEMAP, &fm) == -1)
			return 0;

		if (fm.fiemap.fm_mapped_extents != 1)
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
		int cluster_offset = int(in.StartingVcn.QuadPart % m_cluster_size);

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

				if ((m_open_mode & sparse) == 0)
					set_file_valid_data(m_file_handle, s);

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
			ec.assign(GetLastError(), get_system_category());
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
					ec.assign(GetLastError(), get_system_category());
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

#if TORRENT_HAS_FALLOCATE
			// if fallocate failed, we have to use posix_fallocate
			// which can be painfully slow
			// if you get a compile error here, you might want to
			// define TORRENT_HAS_FALLOCATE to 0.
			ret = posix_fallocate(m_fd, 0, s);
			// posix_allocate fails with EINVAL in case the underlying
			// filesystem does not support this operation
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

	size_type file::get_size(error_code& ec) const
	{
#ifdef TORRENT_WINDOWS
		LARGE_INTEGER file_size;
		if (!GetFileSizeEx(m_file_handle, &file_size))
		{
			ec.assign(GetLastError(), get_system_category());
			return -1;
		}
		return file_size.QuadPart;
#else
		struct stat fs;
		if (fstat(m_fd, &fs) != 0)
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

