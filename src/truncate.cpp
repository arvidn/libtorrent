/*

Copyright (c) 2022, Arvid Norberg
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
		int const fd = ::open(file_path.c_str(), O_RDWR);

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
