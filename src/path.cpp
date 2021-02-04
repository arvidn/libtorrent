/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017-2019, Alden Torres
Copyright (c) 2020, Tiger Wang
Copyright (c) 2020, Kacper Michajłow
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
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/directory.hpp"
#include "libtorrent/string_util.hpp"
#include <cstring>

#include "libtorrent/aux_/escape_string.hpp" // for convert_to_native
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/throw.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <sys/stat.h>
#include <climits> // for IOV_MAX

#ifdef TORRENT_WINDOWS
// windows part

#include "libtorrent/utf8.hpp"
#include "libtorrent/aux_/win_util.hpp"

#include "libtorrent/aux_/windows.hpp"
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

#elif defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
// mac specifics

#include <copyfile.h>

#endif

#endif // posix part

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

	int bufs_size(span<iovec_t const> bufs)
	{
		std::ptrdiff_t size = 0;
		for (auto buf : bufs) size += buf.size();
		return int(size);
	}

#if defined TORRENT_WINDOWS
	std::string convert_from_native_path(wchar_t const* s)
	{
		if (s[0] == L'\\' && s[1] == L'\\' && s[2] == L'?' && s[3] == L'\\') s += 4;
		return convert_from_wstring(s);
	}
#else
	std::string convert_from_native_path(char const* s) { return convert_from_native(s); }
#endif

namespace {
	struct free_function
	{
		void operator()(void* ptr) const noexcept { std::free(ptr); }
	};

	template <typename T>
	std::unique_ptr<T, free_function> make_free_holder(T* ptr)
	{
		return std::unique_ptr<T, free_function>(ptr, free_function{});
	}

#ifdef TORRENT_WINDOWS
	time_t file_time_to_posix(FILETIME f)
	{
		const std::uint64_t posix_time_offset = 11644473600LL;
		std::uint64_t ft = (std::uint64_t(f.dwHighDateTime) << 32)
			| f.dwLowDateTime;

		// windows filetime is specified in 100 nanoseconds resolution.
		// convert to seconds
		return time_t(ft / 10000000 - posix_time_offset);
	}

	void fill_file_status(file_status & s, LARGE_INTEGER file_size, DWORD file_attributes, FILETIME creation_time, FILETIME last_access, FILETIME last_write)
	{
		s.file_size = file_size.QuadPart;
		s.ctime = file_time_to_posix(creation_time);
		s.atime = file_time_to_posix(last_access);
		s.mtime = file_time_to_posix(last_write);

		s.mode = (file_attributes & FILE_ATTRIBUTE_DIRECTORY)
			? file_status::directory
			: (file_attributes & FILE_ATTRIBUTE_DEVICE)
			? file_status::character_special : file_status::regular_file;
	}

	void fill_file_status(file_status & s, DWORD file_size_low, DWORD file_size_high, DWORD file_attributes, FILETIME creation_time, FILETIME last_access, FILETIME last_write)
	{
		LARGE_INTEGER file_size;
		file_size.HighPart = static_cast<LONG>(file_size_high);
		file_size.LowPart = file_size_low;

		fill_file_status(s, file_size, file_attributes, creation_time, last_access, last_write);
	}

#ifdef TORRENT_WINRT
	FILETIME to_file_time(LARGE_INTEGER i)
	{
		FILETIME time;
		time.dwHighDateTime = i.HighPart;
		time.dwLowDateTime = i.LowPart;

		return time;
	}

	void fill_file_status(file_status & s, LARGE_INTEGER file_size, DWORD file_attributes, LARGE_INTEGER creation_time, LARGE_INTEGER last_access, LARGE_INTEGER last_write)
	{
		fill_file_status(s, file_size, file_attributes, to_file_time(creation_time), to_file_time(last_access), to_file_time(last_write));
	}
#endif
#endif
} // anonymous namespace

	native_path_string convert_to_native_path_string(std::string const& path)
	{
#ifdef TORRENT_WINDOWS
#if TORRENT_USE_UNC_PATHS
		// UNC paths must be absolute
		// network paths are already UNC paths
		std::string prepared_path = complete(path);
		if (prepared_path.substr(0,2) != "\\\\")
			prepared_path = "\\\\?\\" + prepared_path;
		std::replace(prepared_path.begin(), prepared_path.end(), '/', '\\');

		return convert_to_wstring(prepared_path);
#else
		return convert_to_wstring(path);
#endif
#else // TORRENT_WINDOWS
		return convert_to_native(path);
#endif
	}

	void stat_file(std::string const& inf, file_status* s
		, error_code& ec, int const flags)
	{
		ec.clear();
		native_path_string f = convert_to_native_path_string(inf);
#ifdef TORRENT_WINDOWS

		WIN32_FILE_ATTRIBUTE_DATA data;
		if (!GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &data))
		{
			ec.assign(GetLastError(), system_category());
			TORRENT_ASSERT(ec);
			return;
		}

		// Fallback to GetFileInformationByHandle for symlinks
		if (!(flags & dont_follow_links) && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		{
#ifdef TORRENT_WINRT

			CREATEFILE2_EXTENDED_PARAMETERS Extended
			{
				sizeof(CREATEFILE2_EXTENDED_PARAMETERS),
				0, // no file attributes
				FILE_FLAG_BACKUP_SEMANTICS // in order to open a directory, we need the FILE_FLAG_BACKUP_SEMANTICS
			};

			const auto h = CreateFile2(f.c_str(), 0, FILE_SHARE_DELETE | FILE_SHARE_READ
				| FILE_SHARE_WRITE, OPEN_EXISTING, &Extended);

			if (h == INVALID_HANDLE_VALUE)
			{
				ec.assign(GetLastError(), system_category());
				TORRENT_ASSERT(ec);
				return;
			}

			FILE_BASIC_INFO Basic;
			FILE_STANDARD_INFO Standard;

			if (
				!GetFileInformationByHandleEx(h, FILE_INFO_BY_HANDLE_CLASS::FileBasicInfo, &Basic, sizeof(FILE_BASIC_INFO)) ||
				!GetFileInformationByHandleEx(h, FILE_INFO_BY_HANDLE_CLASS::FileStandardInfo, &Standard, sizeof(FILE_STANDARD_INFO))
			)
			{
				ec.assign(GetLastError(), system_category());
				TORRENT_ASSERT(ec);
				CloseHandle(h);
				return;
			}
			CloseHandle(h);

			fill_file_status(*s, Standard.EndOfFile, Basic.FileAttributes, Basic.CreationTime, Basic.LastAccessTime, Basic.LastWriteTime);
			return;

#else

			// in order to open a directory, we need the FILE_FLAG_BACKUP_SEMANTICS
			HANDLE h = CreateFileW(f.c_str(), 0, FILE_SHARE_DELETE | FILE_SHARE_READ
				| FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{
				ec.assign(GetLastError(), system_category());
				TORRENT_ASSERT(ec);
				return;
			}

			BY_HANDLE_FILE_INFORMATION handle_data;
			if (!GetFileInformationByHandle(h, &handle_data))
			{
				ec.assign(GetLastError(), system_category());
				TORRENT_ASSERT(ec);
				CloseHandle(h);
				return;
			}
			CloseHandle(h);

			fill_file_status(*s, handle_data.nFileSizeLow, handle_data.nFileSizeHigh, handle_data.dwFileAttributes, handle_data.ftCreationTime, handle_data.ftLastAccessTime, handle_data.ftLastWriteTime);
			return;

#endif
		}

		fill_file_status(*s, data.nFileSizeLow, data.nFileSizeHigh, data.dwFileAttributes, data.ftCreationTime, data.ftLastAccessTime, data.ftLastWriteTime);

#else

		// posix version

		struct ::stat ret{};
		int retval;
		if (flags & dont_follow_links)
			retval = ::lstat(f.c_str(), &ret);
		else
			retval = ::stat(f.c_str(), &ret);
		if (retval < 0)
		{
			ec.assign(errno, system_category());
			return;
		}

		// make sure the _FILE_OFFSET_BITS define worked
		// on this platform. It's supposed to make file
		// related functions support 64-bit offsets.
		static_assert(sizeof(ret.st_size) >= 8, "64 bit file operations are required");

		s->file_size = ret.st_size;
		s->atime = std::uint64_t(ret.st_atime);
		s->mtime = std::uint64_t(ret.st_mtime);
		s->ctime = std::uint64_t(ret.st_ctime);

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

		native_path_string f1 = convert_to_native_path_string(inf);
		native_path_string f2 = convert_to_native_path_string(newf);

		if (f1 == f2) return;

#if defined TORRENT_WINDOWS
#define RenameFunction_ ::_wrename
#else
#define RenameFunction_ ::rename
#endif

		if (RenameFunction_(f1.c_str(), f2.c_str()) < 0)
		{
			ec.assign(errno, generic_category());
		}
#undef RenameFunction_
	}

	void create_directories(std::string const& f, error_code& ec)
	{
		ec.clear();
		if (is_directory(f, ec)) return;
		if (ec != boost::system::errc::no_such_file_or_directory)
			return;
		ec.clear();
		if (is_root_path(f))
		{
			// this is just to set ec correctly, in case this root path isn't
			// mounted
			file_status s;
			stat_file(f, &s, ec);
			return;
		}
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

		native_path_string n = convert_to_native_path_string(f);
#ifdef TORRENT_WINDOWS
		if (CreateDirectoryW(n.c_str(), nullptr) == 0
			&& GetLastError() != ERROR_ALREADY_EXISTS)
			ec.assign(GetLastError(), system_category());
#else
		int ret = ::mkdir(n.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
		if (ret < 0 && errno != EEXIST)
			ec.assign(errno, system_category());
#endif
	}

	void hard_link(std::string const& file, std::string const& link
		, error_code& ec)
	{
		native_path_string n_exist = convert_to_native_path_string(file);
		native_path_string n_link = convert_to_native_path_string(link);
#ifdef TORRENT_WINDOWS
#ifndef TORRENT_WINRT

		BOOL ret = CreateHardLinkW(n_link.c_str(), n_exist.c_str(), nullptr);
		if (ret)
		{
			ec.clear();
			return;
		}
		// something failed. Does the filesystem not support hard links?
		DWORD const error = GetLastError();
		if (error != ERROR_INVALID_FUNCTION)
		{
			// it's possible CreateHardLink will copy the file internally too,
			// if the filesystem does not support it.
			ec.assign(GetLastError(), system_category());
			return;
		}

		// fall back to making a copy
#endif
#else
		// assume posix's link() function exists
		int ret = ::link(n_exist.c_str(), n_link.c_str());

		if (ret == 0)
		{
			ec.clear();
			return;
		}

		// most errors are passed through, except for the ones that indicate that
		// hard links are not supported and require a copy.
		// TODO: 2 test this on a FAT volume to see what error we get!
		if (errno != EMLINK
			&& errno != EXDEV
#ifdef TORRENT_BEOS
			// haiku returns EPERM when the filesystem doesn't support hard link
			&& errno != EPERM
#endif
			)
		{
			// some error happened, report up to the caller
			ec.assign(errno, system_category());
			return;
		}

		// fall back to making a copy

#endif

		// if we get here, we should copy the file
		copy_file(file, link, ec);
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
			for (aux::directory i(old_path, ec); !i.done(); i.next(ec))
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
		native_path_string f1 = convert_to_native_path_string(inf);
		native_path_string f2 = convert_to_native_path_string(newf);

#ifdef TORRENT_WINDOWS

		if (CopyFileW(f1.c_str(), f2.c_str(), false) == 0)
			ec.assign(GetLastError(), system_category());

#elif defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
		// this only works on 10.5
		copyfile_state_t state = copyfile_state_alloc();
		if (copyfile(f1.c_str(), f2.c_str(), state, COPYFILE_ALL) < 0)
			ec.assign(errno, system_category());
		copyfile_state_free(state);
#else
		int const infd = ::open(f1.c_str(), O_RDONLY);
		if (infd < 0)
		{
			ec.assign(errno, system_category());
			return;
		}

		// rely on default umask to filter x and w permissions
		// for group and others
		int const permissions = S_IRUSR | S_IWUSR
			| S_IRGRP | S_IWGRP
			| S_IROTH | S_IWOTH;

		int const outfd = ::open(f2.c_str(), O_WRONLY | O_CREAT, permissions);
		if (outfd < 0)
		{
			close(infd);
			ec.assign(errno, system_category());
			return;
		}
		char buffer[4096];
		for (;;)
		{
			int const num_read = int(read(infd, buffer, sizeof(buffer)));
			if (num_read == 0) break;
			if (num_read < 0)
			{
				ec.assign(errno, system_category());
				break;
			}
			int const num_written = int(write(outfd, buffer, std::size_t(num_read)));
			if (num_written < num_read)
			{
				ec.assign(errno, system_category());
				break;
			}
			if (num_read < int(sizeof(buffer))) break;
		}
		close(infd);
		close(outfd);
#endif // TORRENT_WINDOWS
	}

	void move_file(std::string const& inf, std::string const& newf, error_code& ec)
	{
		ec.clear();

		file_status s;
		stat_file(inf, &s, ec);
		if (ec) return;

		if (has_parent_path(newf))
		{
			create_directories(parent_path(newf), ec);
			if (ec) return;
		}

		rename(inf, newf, ec);
	}

	std::string extension(std::string const& f)
	{
		for (int i = int(f.size()) - 1; i >= 0; --i)
		{
			auto const idx = static_cast<std::size_t>(i);
			if (f[idx] == '/') break;
#ifdef TORRENT_WINDOWS
			if (f[idx] == '\\') break;
#endif
			if (f[idx] != '.') continue;
			return f.substr(idx);
		}
		return "";
	}

	std::string remove_extension(std::string const& f)
	{
		char const* slash = std::strrchr(f.c_str(), '/');
#ifdef TORRENT_WINDOWS
		slash = std::max((char const*)std::strrchr(f.c_str(), '\\'), slash);
#endif
		char const* ext = std::strrchr(f.c_str(), '.');
		// if we don't have an extension, just return f
		if (ext == nullptr || ext == &f[0] || (slash != nullptr && ext < slash)) return f;
		return f.substr(0, aux::numeric_cast<std::size_t>(ext - &f[0]));
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
			for (int j = 2; j < int(f.size()) - 1; ++j)
			{
				if (f[j] != '\\' && f[j] != '/') continue;
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

	bool path_equal(std::string const& lhs, std::string const& rhs)
	{
		std::string::size_type const lhs_size = !lhs.empty()
			&& (lhs[lhs.size()-1] == '/'
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
			|| lhs[lhs.size()-1] == '\\'
#endif
			) ? lhs.size() - 1 : lhs.size();

		std::string::size_type const rhs_size = !rhs.empty()
			&& (rhs[rhs.size()-1] == '/'
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
			|| rhs[rhs.size()-1] == '\\'
#endif
			) ? rhs.size() - 1 : rhs.size();
		return lhs.compare(0, lhs_size, rhs, 0, rhs_size) == 0;
	}

	// <0: lhs < rhs
	//  0: lhs == rhs
	// >0: lhs > rhs
	int path_compare(string_view const lhs, string_view const lfile
		, string_view const rhs, string_view const rfile)
	{
		for (auto lhs_elems = lsplit_path(lhs), rhs_elems = lsplit_path(rhs);
			!lhs_elems.first.empty() || !rhs_elems.first.empty();
			lhs_elems = lsplit_path(lhs_elems.second), rhs_elems = lsplit_path(rhs_elems.second))
		{
			if (lhs_elems.first.empty() || rhs_elems.first.empty())
			{
				if (lhs_elems.first.empty()) lhs_elems.first = lfile;
				if (rhs_elems.first.empty()) rhs_elems.first = rfile;
				return lhs_elems.first.compare(rhs_elems.first);
			}

			int const ret = lhs_elems.first.compare(rhs_elems.first);
			if (ret != 0) return ret;
		}
		return 0;
	}

	bool has_parent_path(std::string const& f)
	{
		if (f.empty()) return false;
		if (is_root_path(f)) return false;

		int len = int(f.size()) - 1;
		// if the last character is / or \ ignore it
		if (f[std::size_t(len)] == '/' || f[std::size_t(len)] == '\\') --len;
		while (len >= 0)
		{
			if (f[std::size_t(len)] == '/' || f[std::size_t(len)] == '\\')
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

		int len = int(f.size());
		// if the last character is / or \ ignore it
		if (f[std::size_t(len - 1)] == '/' || f[std::size_t(len - 1)] == '\\') --len;
		while (len > 0)
		{
			--len;
			if (f[std::size_t(len)] == '/' || f[std::size_t(len)] == '\\')
				break;
		}

		if (f[std::size_t(len)] == '/' || f[std::size_t(len)] == '\\') ++len;
		return std::string(f.c_str(), std::size_t(len));
	}

	std::string filename(std::string const& f)
	{
		if (f.empty()) return "";
		char const* first = f.c_str();
		char const* sep = std::strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		char const* altsep = std::strrchr(first, '\\');
		if (sep == nullptr || altsep > sep) sep = altsep;
#endif
		if (sep == nullptr) return f;

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
					return std::string(sep + 1, std::size_t(len));
				++len;
			}
			return std::string(first, std::size_t(len));

		}
		return std::string(sep + 1);
	}

	void append_path(std::string& branch, string_view leaf)
	{
		TORRENT_ASSERT(!is_complete(leaf));
		if (branch.empty() || branch == ".")
		{
			branch.assign(leaf.data(), leaf.size());
			return;
		}
		if (leaf.empty()) return;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR_CHAR '\\'
		bool const need_sep = branch[branch.size()-1] != '\\'
			&& branch[branch.size()-1] != '/';
#else
#define TORRENT_SEPARATOR_CHAR '/'
		bool const need_sep = branch[branch.size()-1] != '/';
#endif

		if (need_sep) branch += TORRENT_SEPARATOR_CHAR;
		branch.append(leaf.data(), leaf.size());
	}

	std::string combine_path(string_view lhs, string_view rhs)
	{
		TORRENT_ASSERT(!is_complete(rhs));
		if (lhs.empty() || lhs == ".") return rhs.to_string();
		if (rhs.empty() || rhs == ".") return lhs.to_string();

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR "\\"
		bool const need_sep = lhs[lhs.size() - 1] != '\\' && lhs[lhs.size() - 1] != '/';
#else
#define TORRENT_SEPARATOR "/"
		bool const need_sep = lhs[lhs.size() - 1] != '/';
#endif
		std::string ret;
		std::size_t target_size = lhs.size() + rhs.size() + 2;
		ret.resize(target_size);
		target_size = aux::numeric_cast<std::size_t>(std::snprintf(&ret[0], target_size, "%*s%s%*s"
			, int(lhs.size()), lhs.data()
			, (need_sep ? TORRENT_SEPARATOR : "")
			, int(rhs.size()), rhs.data()));
		ret.resize(target_size);
		return ret;
	}

	std::string lexically_relative(string_view base, string_view target)
	{
		// first, strip trailing directory separators
		if (!base.empty() && base.back() == TORRENT_SEPARATOR_CHAR)
			base.remove_suffix(1);
		if (!target.empty() && target.back() == TORRENT_SEPARATOR_CHAR)
			target.remove_suffix(1);

		// strip common path elements
		for (;;)
		{
			if (base.empty()) break;
			string_view prev_base = base;
			string_view prev_target = target;

			string_view base_element;
			string_view target_element;
			std::tie(base_element, base) = split_string(base, TORRENT_SEPARATOR_CHAR);
			std::tie(target_element, target) = split_string(target, TORRENT_SEPARATOR_CHAR);
			if (base_element == target_element) continue;

			base = prev_base;
			target = prev_target;
			break;
		}

		// count number of path elements left in base, and prepend that number of
		// "../" to target

		// base alwaus points to a directory. There's an implied directory
		// separator at the end of it
		int const num_steps = static_cast<int>(std::count(
			base.begin(), base.end(), TORRENT_SEPARATOR_CHAR)) + (base.empty() ? 0 : 1);
		std::string ret;
		for (int i = 0; i < num_steps; ++i)
			ret += ".." TORRENT_SEPARATOR;

		ret += target.to_string();
		return ret;
	}

	std::string current_working_directory()
	{
#if defined TORRENT_WINDOWS
#define GetCurrentDir_ ::_wgetcwd
#else
#define GetCurrentDir_ ::getcwd
#endif
		auto cwd = GetCurrentDir_(nullptr, 0);
		if (cwd == nullptr)
			aux::throw_ex<system_error>(error_code(errno, generic_category()));
		auto holder = make_free_holder(cwd);
		return convert_from_native_path(cwd);
#undef GetCurrentDir_
	}

#if TORRENT_USE_UNC_PATHS
	std::string canonicalize_path(string_view f)
	{
		std::string ret;
		ret.resize(f.size());
		char* write_cur = &ret[0];
		char* last_write_sep = write_cur;

		char const* read_cur = f.data();
		char const* last_read_sep = read_cur;

		// the last_*_sep pointers point to one past
		// the last path separator encountered and is
		// initialized to the first character in the path
		for (int i = 0; i < int(f.size()); ++i)
		{
			if (*read_cur != '\\')
			{
				*write_cur++ = *read_cur++;
				continue;
			}
			int element_len = int(read_cur - last_read_sep);
			if (element_len == 1 && std::memcmp(last_read_sep, ".", 1) == 0)
			{
				--write_cur;
				++read_cur;
				last_read_sep = read_cur;
				continue;
			}
			if (element_len == 2 && std::memcmp(last_read_sep, "..", 2) == 0)
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

	void remove(std::string const& inf, error_code& ec)
	{
		ec.clear();
		native_path_string f = convert_to_native_path_string(inf);

#ifdef TORRENT_WINDOWS
		// windows does not allow trailing / or \ in
		// the path when removing files
		while (!f.empty() && (
			f.back() == '/' ||
			f.back() == '\\'
			)) f.pop_back();

		if (DeleteFileW(f.c_str()) == 0)
		{
			if (GetLastError() == ERROR_ACCESS_DENIED)
			{
				if (RemoveDirectoryW(f.c_str()) != 0)
					return;
			}
			ec.assign(GetLastError(), system_category());
			return;
		}
#else // TORRENT_WINDOWS
		if (::remove(f.c_str()) < 0)
		{
			ec.assign(errno, system_category());
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
			for (aux::directory i(f, ec); !i.done(); i.next(ec))
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

	std::pair<string_view, string_view> rsplit_path(string_view p)
	{
		if (p.empty()) return {{}, {}};
		if (p.back() == TORRENT_SEPARATOR_CHAR) p.remove_suffix(1);
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		else if (p.back() == '/') p.remove_suffix(1);
#endif
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		auto const sep = p.find_last_of("/\\");
#else
		auto const sep = p.find_last_of(TORRENT_SEPARATOR_CHAR);
#endif
		if (sep == string_view::npos) return {{}, p};
		return { p.substr(0, sep), p.substr(sep + 1) };
	}

	std::pair<string_view, string_view> lsplit_path(string_view p)
	{
		if (p.empty()) return {{}, {}};
		// for absolute paths, skip the initial "/"
		if (p.front() == TORRENT_SEPARATOR_CHAR) p.remove_prefix(1);
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		else if (p.front() == '/') p.remove_prefix(1);
#endif
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		auto const sep = p.find_first_of("/\\");
#else
		auto const sep = p.find_first_of(TORRENT_SEPARATOR_CHAR);
#endif
		if (sep == string_view::npos) return {p, {}};
		return { p.substr(0, sep), p.substr(sep + 1) };
	}

	std::pair<string_view, string_view> lsplit_path(string_view p, std::size_t pos)
	{
		if (p.empty()) return {{}, {}};
		// for absolute paths, skip the initial "/"
		if (p.front() == TORRENT_SEPARATOR_CHAR
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
			|| p.front() == '/'
#endif
		)
		{ p.remove_prefix(1); if (pos > 0) --pos; }
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
		auto const sep = find_first_of(p, "/\\", std::string::size_type(pos));
#else
		auto const sep = find_first_of(p, TORRENT_SEPARATOR_CHAR, std::string::size_type(pos));
#endif
		if (sep == string_view::npos) return {p, {}};
		return { p.substr(0, sep), p.substr(sep + 1) };
	}

	std::string complete(string_view f)
	{
		if (is_complete(f)) return f.to_string();
		auto parts = lsplit_path(f);
		if (parts.first == ".") f = parts.second;
		return combine_path(current_working_directory(), f);
	}

	bool is_complete(string_view f)
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
}
