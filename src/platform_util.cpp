/*

Copyright (c) 2020, zywo
Copyright (c) 2010, 2014-2016, 2018, 2020-2021, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/platform_util.hpp"

#include <cstdint>
#include <limits>

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
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

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
}
