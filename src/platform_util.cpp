/*

Copyright (c) 2018, Alden Torres
Copyright (c) 2020, zywo
Copyright (c) 2010, 2014-2018, 2020-2022, Arvid Norberg
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
#include "libtorrent/platform_util.hpp"

#include <cstdint>
#include <limits>

#if TORRENT_HAS_PTHREAD_SET_NAME
#include <pthread.h>
#ifdef TORRENT_BSD
#include <pthread_np.h>
#endif
#endif

#ifdef TORRENT_BEOS
#include <kernel/OS.h>
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if TORRENT_USE_RLIMIT

#include <sys/resource.h>

// capture this here where warnings are disabled (the macro generates warnings)
const rlim_t rlimit_as = RLIMIT_AS;
const rlim_t rlimit_nofile = RLIMIT_NOFILE;
const rlim_t rlim_infinity = RLIM_INFINITY;

#endif // TORRENT_USE_RLIMIT

#ifdef TORRENT_BSD
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined TORRENT_WINDOWS
#include "libtorrent/aux_/windows.hpp"
#include "libtorrent/aux_/win_util.hpp"
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"


namespace libtorrent {

	int max_open_files()
	{
#if defined TORRENT_BUILD_SIMULATOR
		return 256;
#elif TORRENT_USE_RLIMIT

		int const inf = 10000000;

		struct rlimit rl{};
		if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
		{
			if (rl.rlim_cur == rlim_infinity) return inf;
			return rl.rlim_cur <= static_cast<rlim_t>(inf)
				? static_cast<int>(rl.rlim_cur) : inf;
		}
		return 1024;
#else
		// this seems like a reasonable limit for windows.
		// http://blogs.msdn.com/b/oldnewthing/archive/2007/07/18/3926581.aspx
		return 10000;
#endif
	}

	void set_thread_name(char const* name)
	{
		TORRENT_UNUSED(name);
#if TORRENT_HAS_PTHREAD_SET_NAME
#ifdef TORRENT_BSD
		pthread_set_name_np(pthread_self(), name);
#else
		pthread_setname_np(pthread_self(), name);
#endif
#endif
#ifdef TORRENT_WINDOWS
		using SetThreadDescription_t = HRESULT (WINAPI*)(HANDLE, PCWSTR);
		auto SetThreadDescription =
			aux::get_library_procedure<aux::kernel32, SetThreadDescription_t>("SetThreadDescription");
		if (SetThreadDescription) {

			wchar_t wide_name[50];
			int i = -1;
			do {
				++i;
				wide_name[i] = name[i];
			} while (name[i] != 0);
			SetThreadDescription(GetCurrentThread(), wide_name);
		}
#endif
#ifdef TORRENT_BEOS
		rename_thread(find_thread(nullptr), name);
#endif
	}
}
