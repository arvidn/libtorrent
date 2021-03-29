/*

Copyright (c) 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_POINTER_HPP
#define TORRENT_FILE_POINTER_HPP

#include <cstdio>
#include <utility> // for swap
#include <cstdint>

#include "libtorrent/config.hpp"

namespace lt::aux {

struct file_pointer
{
	file_pointer() : ptr(nullptr) {}
	explicit file_pointer(FILE* p) : ptr(p) {}
	~file_pointer() { if (ptr != nullptr) ::fclose(ptr); }
	file_pointer(file_pointer const&) = delete;
	file_pointer(file_pointer&& f) : ptr(f.ptr) { f.ptr = nullptr; }
	file_pointer& operator=(file_pointer const&) = delete;
	file_pointer& operator=(file_pointer&& f)
	{
		std::swap(ptr, f.ptr);
		return *this;
	}
	FILE* file() const { return ptr; }
private:
	FILE* ptr;
};

inline int portable_fseeko(FILE* const f, std::int64_t const offset, int const whence)
{
#ifdef TORRENT_WINDOWS
	return ::_fseeki64(f, offset, whence);
#elif TORRENT_HAS_FSEEKO
	return ::fseeko(f, offset, whence);
#else
	int const fd = ::fileno(f);
	return ::lseek64(fd, offset, whence) == -1 ? -1 : 0;
#endif
}

}

#endif
