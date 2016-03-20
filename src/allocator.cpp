/*

Copyright (c) 2009-2016, Arvid Norberg
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
#include "libtorrent/assert.hpp" // for print_backtrace
#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if defined TORRENT_BEOS
#include <kernel/OS.h>
#include <stdlib.h> // malloc/free
#elif !defined TORRENT_WINDOWS
#include <cstdlib> // posix_memalign/free
#include <unistd.h> // _SC_PAGESIZE
#endif

#if TORRENT_HAVE_MMAP
#include <sys/mman.h>
#endif

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

#if TORRENT_HAVE_MMAP
static void* const map_failed = MAP_FAILED;
#endif

#if TORRENT_USE_MEMALIGN || TORRENT_USE_POSIX_MEMALIGN || defined TORRENT_WINDOWS
#include <malloc.h> // memalign and _aligned_malloc
#include <stdlib.h> // _aligned_malloc on mingw
#endif

#ifdef TORRENT_WINDOWS
// windows.h must be included after stdlib.h under mingw
#include <windows.h>
#endif

#ifdef TORRENT_MINGW
#define _aligned_malloc __mingw_aligned_malloc
#define _aligned_free __mingw_aligned_free
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{

	int page_size()
	{
		static int s = 0;
		if (s != 0) return s;

#ifdef TORRENT_BUILD_SIMULATOR
		s = 4096;
#elif defined TORRENT_WINDOWS
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		s = si.dwPageSize;
#elif defined TORRENT_BEOS
		s = B_PAGE_SIZE;
#else
		s = int(sysconf(_SC_PAGESIZE));
#endif
		// assume the page size is 4 kiB if we
		// fail to query it
		if (s <= 0) s = 4096;
		return s;
	}

	char* page_allocate(std::int64_t const bytes)
	{
		TORRENT_ASSERT(bytes > 0);
		TORRENT_ASSERT(bytes >= std::int64_t(page_size()));

		void* ret;
#if TORRENT_HAVE_MMAP

		int const flags = MAP_PRIVATE | MAP_ANON
#ifdef MAP_NOCORE
		// BSD has a flag to exclude this region from core files
			MAP_NOCORE
#endif
			;

		ret = mmap(0, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (ret == map_failed) return nullptr;
#elif TORRENT_USE_VIRTUAL_ALLOC
		ret = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined TORRENT_BEOS
		area_id id = create_area("", &ret, B_ANY_ADDRESS
			, (bytes + page_size() - 1) & (page_size()-1), B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
		if (id < B_OK) return nullptr;
#elif TORRENT_USE_POSIX_MEMALIGN
		if (posix_memalign(&ret, page_size(), bytes)
			!= 0) return nullptr;
#elif TORRENT_USE_MEMALIGN
		ret = memalign(page_size(), bytes);
#else
		TORRENT_ASSERT(bytes < (std::numeric_limits<size_t>::max)());
		ret = valloc(size_t(bytes));
#endif
		if (ret == nullptr) return nullptr;

		// on version of linux that support it, ask for this region to not be
		// included in coredumps (mostly to make the coredumps more manageable
		// with large disk caches)
#if TORRENT_USE_MADVISE && defined MADV_DONTDUMP
		madvise(ret, bytes, MADV_DONTDUMP);
#endif

		return static_cast<char*>(ret);
	}

	void page_free(char* const block, std::int64_t const size)
	{
		TORRENT_UNUSED(size);
		if (block == nullptr) return;

#if TORRENT_HAVE_MMAP
		int ret = munmap(block, size);
		TORRENT_ASSERT(ret == 0);
		TORRENT_UNUSED(ret);
#elif TORRENT_USE_VIRTUAL_ALLOC
		VirtualFree(block, 0, MEM_RELEASE);
#elif defined TORRENT_WINDOWS
		_aligned_free(block);
#elif defined TORRENT_BEOS
		area_id const id = area_for(block);
		if (id < B_OK) return;
		delete_area(id);
#else // posix_memalign, memalign and valloc all use free()
		::free(block);
#endif // TORRENT_WINDOWS
	}

	void page_dont_need(char* const block, std::int64_t const size)
	{
		TORRENT_UNUSED(block);
		TORRENT_UNUSED(size);
#if TORRENT_USE_MADVISE && defined MADV_FREE
		madvise(block, size, MADV_FREE);
//#elif TORRENT_USE_MADVISE && defined TORRENT_LINUX
//		madvise(block, size, MADV_DONTNEED);
#elif TORRENT_USE_VIRTUAL_ALLOC
		// TODO: 3 MEM_DECOMMIT will not only reove the physical storage behind
		// these virtual addresses, it will also disconnect it from *potential
		// storage*, meaning any futer access to this address range will cause a
		// segmentation fauls (as opposed to a page fault and have the kernel
		// populate it with a new page)
//		VirtualFree(block, size, MEM_DECOMMIT);
#endif
	}
}

