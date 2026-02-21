/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/truncate.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/operations.hpp"

#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/windows.hpp"
#else
#include <sys/stat.h>
#endif

namespace libtorrent {

#ifdef TORRENT_WINDOWS

void truncate_files(file_storage const& fs, std::string const& save_path, storage_error& ec)
{
	for (auto i : fs.file_range())
	{
		if (fs.pad_file_at(i)) continue;
		auto const fn = fs.file_path(i, save_path);
		native_path_string const file_path = convert_to_native_path_string(fn);
#ifdef TORRENT_WINRT
		HANDLE handle = CreateFile2(file_path.c_str()
			, GENERIC_WRITE | GENERIC_READ
			, FILE_SHARE_READ | FILE_SHARE_WRITE
			, OPEN_EXISTING
			, nullptr);
#else
		HANDLE handle = CreateFileW(file_path.c_str()
			, GENERIC_WRITE | GENERIC_READ
			, FILE_SHARE_READ | FILE_SHARE_WRITE
			, nullptr
			, OPEN_EXISTING
			, 0
			, nullptr);
#endif
		if (handle == INVALID_HANDLE_VALUE)
		{
			auto const error = ::GetLastError();
			if (error != ERROR_FILE_NOT_FOUND)
			{
				ec.ec.assign(error, system_category());
				ec.file(i);
				ec.operation = operation_t::file_open;
				return;
			}
			continue;
		}

		LARGE_INTEGER file_size;
		if (GetFileSizeEx(handle, &file_size) == FALSE)
		{
			ec.ec.assign(::GetLastError(), system_category());
			ec.file(i);
			ec.operation = operation_t::file_stat;
			::CloseHandle(handle);
			return;
		}

		if (file_size.QuadPart < fs.file_size(i))
		{
			::CloseHandle(handle);
			continue;
		}

		LARGE_INTEGER sz;
		sz.QuadPart = fs.file_size(i);
		if (SetFilePointerEx(handle, sz, nullptr, FILE_BEGIN) == FALSE)
		{
			ec.ec.assign(::GetLastError(), system_category());
			ec.file(i);
			ec.operation = operation_t::file_seek;
			::CloseHandle(handle);
			return;
		}

		if (::SetEndOfFile(handle) == FALSE)
		{
			ec.ec.assign(::GetLastError(), system_category());
			ec.file(i);
			ec.operation = operation_t::file_truncate;
			::CloseHandle(handle);
			return;
		}
		::CloseHandle(handle);
	}
}

#else

void truncate_files(file_storage const& fs, std::string const& save_path, storage_error& ec)
{
	for (auto i : fs.file_range())
	{
		if (fs.pad_file_at(i)) continue;
		auto const fn = fs.file_path(i, save_path);
		native_path_string const file_path = convert_to_native_path_string(fn);

		int flags = O_RDWR;
#ifdef O_CLOEXEC
		flags |= O_CLOEXEC;
#endif
		int const fd = ::open(file_path.c_str(), flags);

		if (fd < 0)
		{
			int const error = errno;
			if (error != ENOENT)
			{
				ec.ec.assign(error, generic_category());
				ec.file(i);
				ec.operation = operation_t::file_open;
				return;
			}
			continue;
		}

		struct ::stat st;
		if (::fstat(fd, &st) != 0)
		{
			ec.ec.assign(errno, system_category());
			ec.file(i);
			ec.operation = operation_t::file_stat;
			::close(fd);
			return;
		}

		if (st.st_size < fs.file_size(i))
		{
			::close(fd);
			continue;
		}

		if (::ftruncate(fd, static_cast<off_t>(fs.file_size(i))) < 0)
		{
			ec.ec.assign(errno, system_category());
			ec.file(i);
			ec.operation = operation_t::file_truncate;
			::close(fd);
			return;
		}

		::close(fd);
	}
}

#endif

}
