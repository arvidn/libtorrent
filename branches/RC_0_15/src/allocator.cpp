/*

Copyright (c) 2009, Arvid Norberg
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

#include "libtorrent/allocator.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

#ifdef TORRENT_WINDOWS
#include <Windows.h>
#else
#include <stdlib.h>
#include <unistd.h> // _SC_PAGESIZE
#endif

#if TORRENT_USE_MEMALIGN || TORRENT_USE_POSIX_MEMALIGN
#include <malloc.h> // memalign
#endif

#ifdef TORRENT_DEBUG_BUFFERS
#include <sys/mman.h>
#include "libtorrent/size_type.hpp"

struct alloc_header
{
	libtorrent::size_type size;
	int magic;
};

#endif

namespace libtorrent
{
	int page_size()
	{
		static int s = 0;
		if (s != 0) return s;

#ifdef TORRENT_WINDOWS
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		s = si.dwPageSize;
#else
		s = sysconf(_SC_PAGESIZE);
#endif
		// assume the page size is 4 kiB if we
		// fail to query it
		if (s <= 0) s = 4096;
		return s;
	}

	char* page_aligned_allocator::malloc(const size_type bytes)
	{
#ifdef TORRENT_DEBUG_BUFFERS
		int page = page_size();
		char* ret = (char*)valloc(bytes + 2 * page);
		// make the two surrounding pages non-readable and -writable
		TORRENT_ASSERT((bytes & (page-1)) == 0);
		alloc_header* h = (alloc_header*)ret;
		h->size = bytes;
		h->magic = 0x1337;
		mprotect(ret, page, PROT_READ);
		mprotect(ret + page + bytes, page, PROT_READ);
//		fprintf(stderr, "malloc: %p head: %p tail: %p size: %d\n", ret + page, ret, ret + page + bytes, int(bytes));

		return ret + page;
#endif

#if TORRENT_USE_POSIX_MEMALIGN
		void* ret;
		if (posix_memalign(&ret, page_size(), bytes) != 0) ret = 0;
		return reinterpret_cast<char*>(ret);
#elif TORRENT_USE_MEMALIGN
		return reinterpret_cast<char*>(memalign(page_size(), bytes));
#elif defined TORRENT_WINDOWS
		return reinterpret_cast<char*>(VirtualAlloc(0, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
		return reinterpret_cast<char*>(valloc(bytes));
#endif
	}

	void page_aligned_allocator::free(char* const block)
	{

#ifdef TORRENT_DEBUG_BUFFERS
		int page = page_size();
		// make the two surrounding pages non-readable and -writable
		mprotect(block - page, page, PROT_READ | PROT_WRITE);
		alloc_header* h = (alloc_header*)(block - page);
		TORRENT_ASSERT((h->size & (page-1)) == 0);
		TORRENT_ASSERT(h->magic == 0x1337);
		mprotect(block + h->size, page, PROT_READ | PROT_WRITE);
//		fprintf(stderr, "free: %p head: %p tail: %p size: %d\n", block, block - page, block + h->size, int(h->size));
		h->magic = 0;

		::free(block - page);
		return;
#endif

#ifdef TORRENT_WINDOWS
		VirtualFree(block, 0, MEM_RELEASE);
#else
		::free(block);
#endif
	}


}

