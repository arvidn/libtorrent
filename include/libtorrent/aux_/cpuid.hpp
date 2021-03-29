/*

Copyright (c) 2010, 2014-2017, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CPUID_HPP_INCLUDED
#define TORRENT_CPUID_HPP_INCLUDED

#include "libtorrent/config.hpp"

namespace lt::aux {

	// initialized by static initializers (in cpuid.cpp)
	TORRENT_EXTRA_EXPORT extern bool const sse42_support;
	TORRENT_EXTRA_EXPORT extern bool const mmx_support;
	TORRENT_EXTRA_EXPORT extern bool const arm_neon_support;
	TORRENT_EXTRA_EXPORT extern bool const arm_crc32c_support;
}

#endif // TORRENT_CPUID_HPP_INCLUDED
