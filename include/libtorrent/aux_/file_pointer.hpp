/*

Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_FILE_POINTER_HPP
#define TORRENT_FILE_POINTER_HPP

#include <cstdio>
#include <utility> // for swap
#include <cstdint>

#include "libtorrent/config.hpp"

namespace libtorrent {
namespace aux {

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
}

#endif
