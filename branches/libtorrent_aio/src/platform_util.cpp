/*

Copyright (c) 2012-2013, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include <boost/cstdint.hpp>

#if TORRENT_USE_RLIMIT
#include <sys/resource.h>
#endif

#ifdef TORRENT_BSD
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined TORRENT_WINDOWS
#include <windows.h>
#endif

namespace libtorrent
{

	boost::uint64_t total_physical_ram()
	{
		// figure out how much physical RAM there is in
		// this machine. This is used for automatically
		// sizing the disk cache size when it's set to
		// automatic.
		boost::uint64_t ret = 0;
	
#ifdef TORRENT_BSD
#ifdef HW_MEMSIZE
		int mib[2] = { CTL_HW, HW_MEMSIZE };
#else
		// not entirely sure this sysctl supports 64
		// bit return values, but it's probably better
		// than not building
		int mib[2] = { CTL_HW, HW_PHYSMEM };
#endif
		size_t len = sizeof(ret);
		if (sysctl(mib, 2, &ret, &len, NULL, 0) != 0)
			ret = 0;
#elif defined TORRENT_WINDOWS
		MEMORYSTATUSEX ms;
		ms.dwLength = sizeof(MEMORYSTATUSEX);
		if (GlobalMemoryStatusEx(&ms))
			ret = ms.ullTotalPhys;
		else
			ret = 0;
#elif defined TORRENT_LINUX
		ret = sysconf(_SC_PHYS_PAGES);
		ret *= sysconf(_SC_PAGESIZE);
#elif defined TORRENT_AMIGA
		ret = AvailMem(MEMF_PUBLIC);
#endif

#if TORRENT_USE_RLIMIT
		if (ret > 0)
		{
			struct rlimit r;
			if (getrlimit(RLIMIT_AS, &r) == 0 && r.rlim_cur != RLIM_INFINITY)
			{
				if (ret > r.rlim_cur)
					ret = r.rlim_cur;
			}
		}
#endif
		return ret;
	}
}

